/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Offloads tests
 */

/**
 * @defgroup offload-receive_offload Receive Offload for TCP packets
 * @ingroup offload
 * @{
 *
 * @objective Check that when LRO or GRO is enabled, received TCP packets
 *            are coalesced into bigger ones before they reach OS.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *                      - @ref env-peer2peer_ipv6
 * @param rx_csum_on    If @c TRUE, Rx checksum offload is enabled.
 * @param lro_on        If @c TRUE, LRO is enabled.
 * @param gro_on        If @c TRUE, GRO is enabled.
 * @param gro_hw_on     If @c TRUE, Hardware GRO is enabled.
 * @param rx_vlan_strip_on If @c TRUE, VLAN strip is enabled.
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "offload/receive_offload"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_if.h"
#include "tapi_cfg_if_coalesce.h"
#include "tapi_cfg_modules.h"
#include "tapi_tcp.h"
#include "tapi_eth.h"

/** Minimum number of bytes to pass to send() call */
#define MIN_SEND_SIZE 10000
/** Maximum number of bytes to pass to send() call */
#define MAX_SEND_SIZE 20000
/** Minimum number of bytes to send in a single burst */
#define MIN_BURST_SIZE 150000
/** Maximum number of bytes to send in a single burst */
#define MAX_BURST_SIZE 300000
/** Number of bursts */
#define BURSTS_NUM 100
/** Time to wait after a data sending burst, in ms */
#define WAIT_AFTER_SEND 10
/** Maximum time to send or receive, in seconds */
#define TIME_TO_SEND 10

/** Value of rx_coalesce_usecs interrupt coalescing parameter */
#define RX_COALESCE_USECS 300

/** Minimum share of data in coalesced packets */
#define MIN_COALESCED_SHARE 0.15

/**
 * Maximum difference between lro_slow_start_packets driver module
 * parameter and the number of the first coalesced packet when LRO
 * is enabled.
 */
#define MAX_SLOW_START_DIFF 200

/** Auxiliary structure to pass information to/from CSAP callback */
typedef struct pkts_info {
    unsigned int mss;
    unsigned int slow_start_pkts;

    unsigned int pkts_num;
    unsigned int first_big;
    unsigned int big_pkts_num;
    unsigned int max_size;

    long unsigned int pkts_pld;
    long unsigned int slow_start_pld;
    long unsigned int big_pkts_pld;
    te_bool failed;
} pkts_info;

/** Callback to process packets captured by CSAP */
static void
process_pkts(asn_value *pkt, void *arg)
{
    unsigned int pld_len;
    te_errno rc;

    pkts_info *info = (pkts_info *)arg;

    rc = tapi_tcp_get_hdrs_payload_len(pkt, NULL, &pld_len);
    if (rc != 0)
    {
        ERROR("Failed to get packet payload length, rc=%r", rc);
        info->failed = TRUE;
        goto cleanup;
    }

    info->pkts_num++;
    info->pkts_pld += pld_len;
    if (info->pkts_num < info->slow_start_pkts)
        info->slow_start_pld += pld_len;

    if (pld_len > info->mss)
    {
        info->big_pkts_num++;
        if (info->first_big == 0)
            info->first_big = info->pkts_num;

        if (pld_len > info->max_size)
            info->max_size = pld_len;

        info->big_pkts_pld += pld_len;
    }

cleanup:

    asn_free_value(pkt);
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

    te_bool rx_csum_on;
    te_bool lro_on;
    te_bool gro_on;
    te_bool gro_hw_on;
    te_bool rx_vlan_strip_on;
    te_bool lro_works;

    int iut_s = -1;
    int tst_s = -1;
    int mss;

    tapi_pat_sender send_ctx;
    tapi_pat_receiver recv_ctx;
    tarpc_pat_gen_arg *pat_arg = NULL;

    csap_handle_t csap_rx = CSAP_INVALID_HANDLE;
    tapi_tad_trrecv_cb_data csap_cb_data;
    pkts_info stats;

    int lro_slow_start_pkts = 0;
    long unsigned int eligible_data = 0;
    double coalesced_share = 0;

    int i;
    long long unsigned int total_sent = 0;

    char *iut_drv_name = NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BOOL_PARAM(rx_csum_on);
    TEST_GET_BOOL_PARAM(lro_on);
    TEST_GET_BOOL_PARAM(gro_on);
    TEST_GET_BOOL_PARAM(gro_hw_on);
    TEST_GET_BOOL_PARAM(rx_vlan_strip_on);

    TEST_STEP("Set @b rx-checksum, @b rx-lro, @b rx-gro and @b rx-gro-hw "
              "features on the IUT interface according to "
              "values of @p rx_csum_on, @p lro_on, @p gro_on, "
              "@p gro_hw_on and @p rx_vlan_strip_on test parameters.");
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "rx-checksum", rx_csum_on ? 1 : 0);
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "rx-lro", lro_on ? 1 : 0);
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "rx-gro", gro_on ? 1 : 0);
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "rx-gro-hw", gro_hw_on ? 1 : 0);
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "rx-vlan-hw-parse", rx_vlan_strip_on ? 1 : 0);

    /* Rx checksum offload is needed for LRO to work */
    lro_works = (lro_on && rx_csum_on);

    CHECK_NOT_NULL(iut_drv_name = net_drv_driver_name(iut_rpcs->ta));
    rc = tapi_cfg_module_param_get_int(iut_rpcs->ta,
                                       iut_drv_name,
                                       "lro_slow_start_packets",
                                       &lro_slow_start_pkts);
    if (rc == TE_RC(TE_CS, TE_ENOENT))
    {
        WARN("lro_slow_start_packets parameter is not supported");
    }
    else if (rc != 0)
    {
        TEST_VERDICT("Unexpected error when trying to get "
                     "lro_slow_start_packets parameter value: %r", rc);
    }

    TEST_STEP("Try to disable adaptive Rx interrupt coalescing and set "
              "@b rx_coalesce_usecs to a big value on the IUT "
              "interface to make sure that LRO or GRO often coalesces "
              "incoming packets.");
    /*
     * This may be not necessary/possible for all drivers, therefore
     * errors are ignored here.
     */
    tapi_cfg_if_coalesce_set(iut_rpcs->ta, iut_if->if_name,
                             "use_adaptive_rx_coalesce", 0);
    tapi_cfg_if_coalesce_set(iut_rpcs->ta, iut_if->if_name,
                             "rx_coalesce_usecs", RX_COALESCE_USECS);

    TEST_STEP("Make sure TSO is enabled on Tester if possible, so that "
              "packets are sent as fast as possible from it.");
    /* Ignore errors here as this is not essential */
    net_drv_try_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                               "tx-checksum-ip-generic", 1);
    if (iut_addr->sa_family == AF_INET)
    {
        net_drv_try_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                                   "tx-checksum-ipv4", 1);
        net_drv_try_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                                   "tx-tcp-segmentation", 1);
    }
    else
    {
        net_drv_try_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                                   "tx-checksum-ipv6", 1);
        net_drv_try_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                                   "tx-tcp6-segmentation", 1);
    }

    TEST_STEP("If @p rx_vlan_strip_on, create VLANs, assign addresses and "
              "use it for traffic checks below.");
    if (rx_vlan_strip_on)
    {
        struct sockaddr *iut_addr2 = NULL;
        struct sockaddr *tst_addr2 = NULL;

        net_drv_ts_add_vlan(iut_rpcs->ta, tst_rpcs->ta,
                            iut_if->if_name, tst_if->if_name,
                            iut_addr->sa_family, NULL,
                            &iut_addr2, &tst_addr2);

        te_sockaddr_set_port(iut_addr2, te_sockaddr_get_port(iut_addr));
        iut_addr = iut_addr2;
        te_sockaddr_set_port(tst_addr2, te_sockaddr_get_port(tst_addr));
        tst_addr = tst_addr2;
    }

    CFG_WAIT_CHANGES;

    TEST_STEP("Establish connection between a pair of TCP sockets "
              "on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_STREAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Enable @c TCP_NODELAY option on the Tester socket so "
              "that packets are sent as soon as possible from it.");
    rpc_setsockopt_int(tst_rpcs, tst_s, RPC_TCP_NODELAY, 1);

    TEST_STEP("Create a CSAP on IUT to capture packets sent from Tester.");
    CHECK_RC(tapi_tcp_ip_eth_csap_create(
        iut_rpcs->ta, 0,
        iut_if->if_name,
        TAD_ETH_RECV_DEF |
        TAD_ETH_RECV_NO_PROMISC,
        NULL, NULL,
        iut_addr->sa_family,
        TAD_SA2ARGS(iut_addr, tst_addr),
        &csap_rx));

    CHECK_RC(tapi_tad_trrecv_start(iut_rpcs->ta, 0, csap_rx,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    TEST_STEP("Send a lot of data from Tester in multiple bursts with help "
              "of @b rpc_pattern_sender(), receiving it all on IUT with "
              "@b rpc_pattern_receiver().");

    tapi_pat_sender_init(&send_ctx);
    tapi_pat_receiver_init(&recv_ctx);
    send_ctx.time2wait = TAPI_WAIT_NETWORK_DELAY;
    recv_ctx.time2wait = TAPI_WAIT_NETWORK_DELAY;
    send_ctx.duration_sec = TIME_TO_SEND;
    recv_ctx.duration_sec = TIME_TO_SEND;
    send_ctx.iomux = FUNC_NO_IOMUX;

    tapi_rand_gen_set(&send_ctx.size, MIN_SEND_SIZE, MAX_SEND_SIZE, FALSE);

    send_ctx.gen_func = RPC_PATTERN_GEN_LCG;
    recv_ctx.gen_func = RPC_PATTERN_GEN_LCG;
    pat_arg = &send_ctx.gen_arg;
    pat_arg->offset = 0;
    pat_arg->coef1 = rand_range(0, RAND_MAX);
    pat_arg->coef2 = rand_range(1, RAND_MAX);
    pat_arg->coef3 = rand_range(0, RAND_MAX);
    memcpy(&recv_ctx.gen_arg, pat_arg, sizeof(*pat_arg));

    iut_rpcs->op = RCF_RPC_CALL;
    rpc_pattern_receiver(iut_rpcs, iut_s, &recv_ctx);

    for (i = 0; i < BURSTS_NUM; i++)
    {
        send_ctx.total_size = rand_range(MIN_BURST_SIZE,
                                         MAX_BURST_SIZE);

        RPC_AWAIT_ERROR(tst_rpcs);
        rc = rpc_pattern_sender(tst_rpcs, tst_s, &send_ctx);
        if (rc < 0)
        {
            ERROR_VERDICT("rpc_pattern_sender() failed on Tester with "
                          "error " RPC_ERROR_FMT, RPC_ERROR_ARGS(tst_rpcs));
            rpc_pattern_receiver(iut_rpcs, iut_s, &recv_ctx);
            TEST_STOP;
        }

        total_sent += send_ctx.sent;
        MSLEEP(WAIT_AFTER_SEND);
    }

    RING("%llu bytes were sent", total_sent);

    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_pattern_receiver(iut_rpcs, iut_s, &recv_ctx);
    if (rc < 0)
    {
        TEST_VERDICT("rpc_pattern_receiver() failed on IUT with "
                     "error " RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    if (total_sent != recv_ctx.received)
    {
        TEST_VERDICT("Number of bytes received does not match "
                     "number of bytes sent");
    }

    rpc_getsockopt(iut_rpcs, iut_s, RPC_TCP_MAXSEG, &mss);

    memset(&csap_cb_data, 0, sizeof(csap_cb_data));
    memset(&stats, 0, sizeof(stats));
    csap_cb_data.callback = &process_pkts;
    csap_cb_data.user_data = &stats;
    stats.mss = mss;
    if (lro_slow_start_pkts > 0)
        stats.slow_start_pkts = lro_slow_start_pkts;

    TEST_STEP("Process packets captured by CSAP on IUT. Check that "
              "larger-than-MSS packets are encountered if and only if "
              "either @p gro_on and/or gro_hw_on are @c TRUE or both "
              "@p lro_on and @p rx_csum_on are @c TRUE.");
    TEST_SUBSTEP("If LRO is enabled (i.e. both @p lro_on and @p rx_csum_on "
                 "are @c TRUE) and 'sfc' driver is tested, check that the "
                 "first large packet is received after a number of packets "
                 "specified by @b lro_slow_start_packets driver module "
                 "parameter.");

    rcf_tr_op_log(FALSE);
    CHECK_RC(tapi_tad_trrecv_stop(iut_rpcs->ta, 0, csap_rx,
                                  &csap_cb_data, NULL));

    RING("%u packets with %lu bytes were received, of them %u packets "
         "with %lu bytes were coalesced (larger than MSS) with maximum "
         "size of %u bytes", stats.pkts_num, stats.pkts_pld,
         stats.big_pkts_num, stats.big_pkts_pld, stats.max_size);
    if (stats.first_big > 0)
    {
        RING("The first big packet came after %u normal packets",
             stats.first_big - 1);
    }

    if (stats.failed)
        TEST_VERDICT("Failed to process some of the packets");
    else if (stats.pkts_num == 0)
        TEST_VERDICT("No packets were captured");

    if (!lro_works && !gro_on && !gro_hw_on)
    {
        if (stats.first_big > 0)
        {
            if (lro_on && !rx_csum_on)
            {
                RING_VERDICT("Some packets are coalesced when LRO "
                             "is enabled even if Rx checksumming is "
                             "turned off");
            }
            else
            {
                TEST_VERDICT("A large packet was captured while both "
                             "LRO and GRO are disabled");
            }
        }
    }
    else
    {
        if (stats.first_big == 0)
        {
            TEST_VERDICT("No large packets were captured while "
                         "LRO and/or GRO were enabled");
        }

        eligible_data = stats.pkts_pld;
        if (lro_works && lro_slow_start_pkts > 0)
        {
            if ((int)(stats.first_big) <
                    lro_slow_start_pkts - MAX_SLOW_START_DIFF)
            {
                ERROR_VERDICT("The first large packet came earlier than "
                              "lro_slow_start_packets parameter allows");
            }

            eligible_data -= stats.slow_start_pld;
            RING("Only last %lu received bytes are counted when computing "
                 "coalesced data share because of lro_slow_start_packets "
                 "parameter", eligible_data);
        }

        coalesced_share = (double)(stats.big_pkts_pld) / eligible_data;
        RING("%.2f%% of received bytes were in coalesced packets",
             coalesced_share * 100.0);

        if (coalesced_share < MIN_COALESCED_SHARE)
            ERROR_VERDICT("Too few packets were coalesced");
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0,
                                           csap_rx));

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);
    free(iut_drv_name);

    TEST_END;
}

/** @} */
