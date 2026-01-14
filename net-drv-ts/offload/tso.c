/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Offloads tests
 */

/**
 * @defgroup offload-tso TCP Segmentation Offload
 * @ingroup offload
 * @{
 *
 * @objective Check that when TSO is enabled, larger-than-MSS packets are
 *            passed to NIC and split into MSS-sized ones by it.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *                      - @ref env-peer2peer_ipv6
 * @param tso_on        Should TCP Segmentation Offload be enabled?
 * @param min_size      Minimum size of data passed to send() call, in
 *                      MTU units:
 *                      - @c 2
 * @param max_size      Maximum size of data passed to send() call, in
 *                      MTU units:
 *                      - @c 5
 * @param send_calls    Number of send() calls:
 *                      - @c 3
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "offload/tso"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_if.h"
#include "tapi_tcp.h"
#include "tapi_eth.h"
#include "te_vector.h"

/** Auxiliary structure to store information for TCP segments. */
typedef struct tcp_seg_info {
    uint32_t seqn;
    unsigned int len;
} tcp_seg_info;

/** Auxiliary structure to pass information to/from CSAP callback */
typedef struct pkts_info {
    unsigned int mss;
    unsigned int pkts_num;

    uint32_t first_seqn;
    uint32_t last_seqn;
    uint32_t total_len;

    te_bool large_pkts_detected;
    te_bool failed;

    te_vec segs;
} pkts_info;

/** Callback to process packets captured by CSAP */
static void
process_pkts(asn_value *pkt, void *arg)
{
    pkts_info *info = (pkts_info *)arg;
    unsigned int pld_len;
    unsigned int total_len;
    tcp_seg_info seg_info;
    uint32_t seqn;
    te_errno rc;

    info->pkts_num++;

    rc = asn_read_uint32(pkt, &seqn, "pdus.0.#tcp.seqn");
    if (rc != 0)
    {
        ERROR("Failed to get SEQN: %r", rc);
        info->failed = TRUE;
        goto cleanup;
    }

    rc = tapi_tcp_get_hdrs_payload_len(pkt, NULL, &pld_len);
    if (rc != 0)
    {
        ERROR("Failed to get packet payload length, rc=%r", rc);
        info->failed = TRUE;
        goto cleanup;
    }

    if (info->pkts_num == 1)
    {
        info->first_seqn = seqn;
        info->last_seqn = seqn;
        info->total_len = pld_len;
    }
    else
    {
        if (tapi_tcp_compare_seqn(seqn, info->last_seqn) > 0)
            info->last_seqn = seqn;

        if (tapi_tcp_compare_seqn(seqn, info->first_seqn) < 0)
        {
            info->total_len += (info->first_seqn - seqn);
            info->first_seqn = seqn;
        }

        total_len = (seqn - info->first_seqn) + pld_len;
        if (total_len > info->total_len)
            info->total_len = total_len;
    }

    seg_info.seqn = seqn;
    seg_info.len = pld_len;
    TE_VEC_APPEND(&info->segs, seg_info);

    RING("Captured packet with TCP payload of %u bytes which is "
         "%s MSS", pld_len,
         pld_len > info->mss ?
                "bigger than" :
                 (pld_len == info->mss ?
                            "equal to" : "smaller than"));

    if (pld_len > info->mss)
        info->large_pkts_detected = TRUE;

cleanup:

    asn_free_value(pkt);
}

/** Perform generic checks after processing packets from CSAP */
static void
check_csap_gen(pkts_info *info, unsigned int sent, const char *csap_name)
{
    if (info->failed)
    {
        TEST_VERDICT("Failed to process captured packet(s) from %s CSAP",
                     csap_name);
    }

    if (info->pkts_num == 0)
        TEST_VERDICT("No packets were captured on %s CSAP", csap_name);

    if (info->total_len != sent)
    {
        ERROR("%u bytes were sent, %u bytes were captured by %s CSAP",
              sent, info->total_len, csap_name);
        TEST_VERDICT("Number of bytes in packets captured by %s CSAP "
                     "does not match number of bytes sent", csap_name);
    }
}

/** Split Tx packets by MSS. */
static void
split_pkts_by_mss(const te_vec *tx_segs, unsigned int mss, te_vec *expected)
{
    size_t i;

    te_vec_reset(expected);

    for (i = 0; i < te_vec_size(tx_segs); i++)
    {
        const tcp_seg_info seg_info = TE_VEC_GET(tcp_seg_info, tx_segs, i);
        unsigned int off = 0;

        while (off < seg_info.len)
        {
            unsigned int cur_len = seg_info.len - off;
            tcp_seg_info seg_out;

            if (cur_len > mss)
                cur_len = mss;

            seg_out.seqn = seg_info.seqn + off;
            seg_out.len = cur_len;
            TE_VEC_APPEND(expected, seg_out);

            off += cur_len;
        }
    }
}

/** Compare Rx packets' info with expected one. */
static void
check_rx_pkts(te_vec *rx_segs, te_vec *expected)
{
    size_t exp_num = te_vec_size(expected);
    size_t rx_num = te_vec_size(rx_segs);
    bool fail = false;
    size_t check_num;
    size_t i;

    if (rx_num != exp_num)
    {
        fail = true;
        ERROR("Expected %u TCP segments on Tester after splitting, got %u",
              exp_num, rx_num);
        ERROR_VERDICT("Unexpected number of TCP segments captured on Tester");
    }

    check_num = MIN(exp_num, rx_num);

    for (i = 0; i < check_num; i++)
    {
        tcp_seg_info exp = TE_VEC_GET(tcp_seg_info, expected, i);
        tcp_seg_info got = TE_VEC_GET(tcp_seg_info, rx_segs, i);

        if (tapi_tcp_compare_seqn(got.seqn, exp.seqn) != 0 ||
            got.len != exp.len)
        {
            fail = true;
            ERROR("Tester CSAP: mismatch at index %u: expected {seq=%u len=%u}, got {seq=%u len=%u}",
                  i, exp.seqn, exp.len, got.seqn, got.len);
        }
    }

    if (fail)
        TEST_VERDICT("TCP segments captured on Tester do not match expected TSO splitting result");
}

/**
 * Check packets captured by two CSAPs - one capturing outgoing
 * packets on sender and another one capturing incoming packets
 * on receiver. If TSO is enabled, bigger-than-MSS packets should
 * be observed on sender. On receiver, all packets must be of MSS
 * size or less.
 */
static void
check_captured_pkts(const char *ta_tx, csap_handle_t csap_tx,
                    const char *ta_rx, csap_handle_t csap_rx,
                    unsigned int send_len, int mss,
                    te_bool tso_on)
{
    tapi_tad_trrecv_cb_data csap_cb_data;
    pkts_info tx_stats = { .mss = (unsigned int)mss,
                           .segs = TE_VEC_INIT(tcp_seg_info) };
    pkts_info rx_stats = { .mss = (unsigned int)mss,
                           .segs = TE_VEC_INIT(tcp_seg_info) };
    te_vec expected = TE_VEC_INIT(tcp_seg_info);

    memset(&csap_cb_data, 0, sizeof(csap_cb_data));
    csap_cb_data.callback = &process_pkts;
    csap_cb_data.user_data = &tx_stats;

    CHECK_RC(tapi_tad_trrecv_get(ta_tx, 0, csap_tx,
                                 &csap_cb_data, NULL));
    check_csap_gen(&tx_stats, send_len, "IUT");

    if (tso_on)
    {
        if (!tx_stats.large_pkts_detected)
        {
            TEST_VERDICT("TSO is on, however no bigger than MSS Tx packets "
                         "were captured");
        }
    }
    else
    {
        if (tx_stats.large_pkts_detected)
        {
            TEST_VERDICT("TSO is off, however bigger than MSS Tx packets "
                         "were captured");
        }
    }

    csap_cb_data.user_data = &rx_stats;
    CHECK_RC(tapi_tad_trrecv_get(ta_rx, 0, csap_rx,
                                 &csap_cb_data, NULL));

    check_csap_gen(&rx_stats, send_len, "Tester");

    if (rx_stats.large_pkts_detected)
        TEST_VERDICT("Larger than MSS packets were captured on Tester");

    split_pkts_by_mss(&tx_stats.segs, (unsigned int)mss, &expected);
    check_rx_pkts(&rx_stats.segs, &expected);

    te_vec_free(&expected);
    te_vec_free(&tx_stats.segs);
    te_vec_free(&rx_stats.segs);
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;

    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    int iut_s = -1;
    int tst_s = -1;
    int tst_listener = -1;

    const char *proto_csum_feature = NULL;
    const char *tso_feature = NULL;
    int mtu;
    int mss;
    char *send_buf = NULL;
    size_t send_len;
    te_dbuf recv_dbuf = TE_DBUF_INIT(0);

    csap_handle_t csap_rx = CSAP_INVALID_HANDLE;
    csap_handle_t csap_tx = CSAP_INVALID_HANDLE;

    te_bool tso_on;
    te_bool vlan_hw_insert_on;
    int min_size;
    int max_size;
    int send_calls;
    int i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BOOL_PARAM(tso_on);
    TEST_GET_BOOL_PARAM(vlan_hw_insert_on);
    TEST_GET_INT_PARAM(min_size);
    TEST_GET_INT_PARAM(max_size);
    TEST_GET_INT_PARAM(send_calls);

    if (iut_addr->sa_family == AF_INET)
    {
        proto_csum_feature = "tx-checksum-ipv4";
        tso_feature = "tx-tcp-segmentation";
    }
    else
    {
        proto_csum_feature = "tx-checksum-ipv6";
        tso_feature = "tx-tcp6-segmentation";
    }

    if (tso_on)
    {
        TEST_STEP("If @p tso_on is @c TRUE, try to enable related Tx "
                  "checksum offloadings if they are disabled.");

        net_drv_try_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                                   "tx-checksum-ip-generic", 1);
        net_drv_try_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                                   proto_csum_feature, 1);
    }

    TEST_STEP("Turn TSO on or off on IUT according to @p tso_on.");
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           tso_feature, tso_on ? 1 : 0);

    TEST_STEP("Turn Tx VLAN HW insertion on or off on IUT according to @p vlan_hw_insert_on.");
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "tx-vlan-hw-insert", vlan_hw_insert_on ? 1 : 0);

    TEST_STEP("Make sure LRO and GRO are turned off on Tester, so that "
              "CSAP captures IUT packets there exactly as they were sent "
              "and their sizes can be checked.");
    net_drv_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                           "rx-gro", 0);
    net_drv_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                           "rx-gro-hw", 0);
    net_drv_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                           "rx-lro", 0);

    TEST_STEP("If @p rx_vlan_strip_on, create VLANs, assign addresses and "
              "use it for traffic checks below.");
    if (vlan_hw_insert_on)
    {
        struct sockaddr *iut_addr2 = NULL;
        struct sockaddr *tst_addr2 = NULL;

        net_drv_ts_add_vlan(iut_rpcs->ta, tst_rpcs->ta,
                            iut_if->if_name, tst_if->if_name,
                            iut_addr->sa_family, NULL,
                            &iut_addr2, &tst_addr2);

        CFG_WAIT_CHANGES;

        te_sockaddr_set_port(iut_addr2, te_sockaddr_get_port(iut_addr));
        iut_addr = iut_addr2;
        te_sockaddr_set_port(tst_addr2, te_sockaddr_get_port(tst_addr));
        tst_addr = tst_addr2;
    }

    CHECK_RC(tapi_cfg_base_if_get_mtu_u(iut_rpcs->ta, iut_if->if_name,
                                        &mtu));

    TEST_STEP("Establish connection between a pair of TCP sockets "
              "on IUT and Tester, setting @c SO_SNDBUF on the IUT "
              "socket and @c SO_RCVBUF on the Tester socket to "
              "values large enough for the data to be sent.");

    iut_s = rpc_socket(iut_rpcs, rpc_socket_domain_by_addr(iut_addr),
                       RPC_SOCK_STREAM, RPC_PROTO_DEF);
    tst_listener = rpc_socket(tst_rpcs, rpc_socket_domain_by_addr(tst_addr),
                              RPC_SOCK_STREAM, RPC_PROTO_DEF);
    rpc_bind(iut_rpcs, iut_s, iut_addr);
    rpc_bind(tst_rpcs, tst_listener, tst_addr);

    rpc_setsockopt_int(iut_rpcs, iut_s, RPC_SO_SNDBUF, mtu * max_size);
    rpc_setsockopt_int(tst_rpcs, tst_listener, RPC_SO_RCVBUF,
                       mtu * max_size);

    rpc_listen(tst_rpcs, tst_listener, -1);
    rpc_connect(iut_rpcs, iut_s, tst_addr);
    tst_s = rpc_accept(tst_rpcs, tst_listener, NULL, NULL);

    TEST_STEP("Create a CSAP on IUT to capture outgoing packets.");

    CHECK_RC(tapi_tcp_ip_eth_csap_create(
        iut_rpcs->ta, 0,
        iut_if->if_name,
        TAD_ETH_RECV_OUT |
        TAD_ETH_RECV_NO_PROMISC,
        NULL, NULL,
        iut_addr->sa_family,
        TAD_SA2ARGS(tst_addr, iut_addr),
        &csap_tx));

    CHECK_RC(tapi_tad_trrecv_start(iut_rpcs->ta, 0, csap_tx,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    TEST_STEP("Create a CSAP on Tester to capture incoming packets.");

    CHECK_RC(tapi_tcp_ip_eth_csap_create(
        tst_rpcs->ta, 0,
        tst_if->if_name,
        TAD_ETH_RECV_DEF |
        TAD_ETH_RECV_NO_PROMISC,
        NULL, NULL,
        tst_addr->sa_family,
        TAD_SA2ARGS(tst_addr, iut_addr),
        &csap_rx));

    CHECK_RC(tapi_tad_trrecv_start(tst_rpcs->ta, 0, csap_rx,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    rpc_getsockopt(tst_rpcs, tst_s, RPC_TCP_MAXSEG, &mss);

    TEST_STEP("Do @p send_calls times in a loop:");

    for (i = 0; i < send_calls; i++)
    {
        TEST_SUBSTEP("Pass a single bigger than MTU buffer to send() on "
                     "the IUT socket.");
        send_buf = te_make_buf(mtu * min_size, mtu * max_size, &send_len);
        RPC_SEND(rc, iut_rpcs, iut_s, send_buf, send_len, 0);

        TEST_SUBSTEP("Receive and check data on the Tester socket.");
        rpc_read_fd2te_dbuf(tst_rpcs, tst_s, TAPI_WAIT_NETWORK_DELAY,
                            0, &recv_dbuf);

        if (recv_dbuf.len == 0)
            TEST_VERDICT("Tester received no data");
        else if (recv_dbuf.len != send_len)
            TEST_VERDICT("Tester received unexpected amount of data");
        else if (memcmp(recv_dbuf.ptr, send_buf, send_len) != 0)
            TEST_VERDICT("Data received does not match data sent");

        te_dbuf_free(&recv_dbuf);
        free(send_buf);
        send_buf = NULL;

        TEST_SUBSTEP("Examine outgoing packets captured by the IUT CSAP. "
                     "Packets bigger than MSS should be encountered if and "
                     "only if @p tso_on is @c TRUE.");
        TEST_SUBSTEP("Examine incoming packets captured by the Tester "
                     "CSAP. All the captured packets except the last one "
                     "must be of MSS size.");
        check_captured_pkts(iut_rpcs->ta, csap_tx, tst_rpcs->ta, csap_rx,
                            send_len, mss, tso_on);
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0,
                                           csap_tx));

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0,
                                           csap_rx));

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_listener);

    te_dbuf_free(&recv_dbuf);
    free(send_buf);

    TEST_END;
}

/** @} */
