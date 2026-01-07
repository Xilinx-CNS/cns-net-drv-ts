/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/* Copyright (C) 2024 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Rx path tests
 */

/**
 * @defgroup rx_path-rx_coalesce_frames Setting rx_max_coalesced_frames to a specific value
 * @ingroup rx_path
 * @{
 *
 * @objective Check that when @b rx_max_coalesced_frames is set to a specific
 *            value, Rx interrupts coalescing works accordingly.
 *
 * @param env             Testing environment:
 *                        - @ref env-peer2peer
 *                        - @ref env-peer2peer_ipv6
 * @param coalesce_frames Value to set for @b rx_max_coalesced_frames:
 *                        - @c 13
 *                        - @c 100
 *                        - @c 1000
 * @param send_delay      Delay between periodically sent packets:
 *                        - @c 10
 *
 * @par Scenario:
 *
 * @author Andrew Rybchenko <andrew.rybchenko@oktetlabs.ru>
 */

#define TE_TEST_NAME "rx_path/rx_coalesce_frames"

#include "net_drv_test.h"
#include "tapi_eth.h"
#include "tapi_udp.h"
#include "tapi_cfg_if_coalesce.h"
#include "tapi_mem.h"

#include <math.h>
#include <byteswap.h>

/**
 * Minimum delay between packets after which the next packet
 * is considered to be from another packets group, microseconds.
 */
#define MIN_GROUP_DELAY 100

/**
 * Maximum relative deviation of actual packet distrubition
 * parameter value from expected one.
 */
#define ERR_MARGIN 0.10

/** Minimum delay between packet sends on Tester, in microseconds. */
#define MIN_SEND_DELAY 10

/** How long to send packets from Tester to warm up, in milliseconds */
#define WARNUP_TIME_MS 100

/** How long to send packets from Tester, in milliseconds */
#define SEND_TIME 2000

/** Data passed to and from CSAP callback */
typedef struct test_cb_args {
    /** Value of rx_max_coalesced_frames */
    unsigned int coalesce_frames;
    /** Delay after sending a packet on Tester */
    unsigned int pkt_send_delay;

    /**
     * Numbers of Tx packets with too big delays before
     * them
     */
    te_vec delayed_pkt_ids;
    /** Current element in delayed_pkt_ids vector. */
    unsigned int cur_delayed_idx;

    /** Timestamp of the previous packet */
    struct timeval prev_ts;
    /** Size the current packet group */
    unsigned int cur_group_size;

    /** Number of captured packets */
    unsigned int n_pkts;
    /** Id of the last captured packet */
    uint64_t cur_pkt_id;
    /** Number of packet groups */
    unsigned int n_groups;
    /** Total number of packets in all Rx packet groups */
    unsigned int total_group_size;
    /**
     * Sum of squared deviations of actual packet group sizes
     * from rx_max_coalesced_frames
     */
    double squared_dev_sum;
    /** Average size of a packet group */
    double avg_group_size;
    /**
     * Square root of average squared deviation of packet group
     * size from rx_max_coalesced_frames
     */
    double group_size_dev;

    /** Will be set to TRUE if packet processing failed */
    te_bool failed;
} test_cb_args;

/** Initializer for test_cb_args */
#define TEST_CB_ARGS_INIT \
    {                                               \
      .coalesce_frames = 0,                         \
      .pkt_send_delay = 0,                          \
      .delayed_pkt_ids = TE_VEC_INIT(uint64_t),     \
      .cur_delayed_idx = 0,                         \
      .prev_ts = { 0, },                            \
      .cur_group_size = 0,                          \
      .n_pkts = 0,                                  \
      .cur_pkt_id = 0,                              \
      .n_groups = 0,                                \
      .total_group_size = 0,                        \
      .squared_dev_sum = 0.0,                       \
      .avg_group_size = 0.0,                        \
      .group_size_dev = 0.0,                        \
      .failed = FALSE,                              \
    }

/**
 * Finalize the current packet group, update related
 * statistics.
 */
static void
finish_pkt_group(test_cb_args *args)
{
    unsigned int group_size;
    double size_deviation;
    te_bool skip_group = FALSE;
    unsigned int tx_pkt_id;
    unsigned int tx_delays_num;

    tx_delays_num = te_vec_size(&args->delayed_pkt_ids);
    if (args->cur_delayed_idx < tx_delays_num)
    {
        tx_pkt_id = TE_VEC_GET(uint64_t, &args->delayed_pkt_ids,
                               args->cur_delayed_idx);
        if (args->cur_pkt_id >= tx_pkt_id)
        {
            /*
             * If the current packet was sent after an unexpectedly big
             * delay, it is hard to estimate real size of the
             * last packet group, so ignore it.
             */
            skip_group = TRUE;

            for (args->cur_delayed_idx++;
                 args->cur_delayed_idx < tx_delays_num;
                 args->cur_delayed_idx++)
            {
                tx_pkt_id = TE_VEC_GET(uint64_t, &args->delayed_pkt_ids,
                                       args->cur_delayed_idx);
                if (tx_pkt_id > args->cur_pkt_id)
                    break;
            }
        }
    }

    if (!skip_group)
    {
        args->n_groups++;

        group_size = args->cur_group_size;
        args->total_group_size += group_size;
        size_deviation = (int)group_size - (int)args->coalesce_frames;
        args->squared_dev_sum += size_deviation * size_deviation;
    }

    args->cur_group_size = 0;
}

/**
 * Get Id of the packet (64-bit ordinal number stored in its payload).
 */
static te_errno
get_pkt_id(asn_value *pkt, uint64_t *id)
{
    int pld_len;
    size_t field_len;
    char *buf = NULL;
    int i;
    te_errno rc;

    pld_len = asn_get_length(pkt, "payload.#bytes");
    if (pld_len < 0)
    {
        ERROR("%s(): failed to get length of payload.#bytes",
              __FUNCTION__);
        return TE_EBADMSG;
    }

    buf = tapi_calloc(pld_len, 1);

    field_len = pld_len;
    rc = asn_read_value_field(pkt, buf, &field_len, "payload.#bytes");
    if (rc != 0)
    {
        ERROR("%s(): failed to read payload.#bytes, rc=%r",
              __FUNCTION__, rc);
        free(buf);
        return rc;
    }

    /*
     * Skip padding which may be present in short Ethernet frame.
     * Packet Id is always followed by 0xff byte to make this possible.
     */
    for (i = field_len - 1; i >= 0 && buf[i] == 0; i--);
    if (i < (int)sizeof(uint64_t))
    {
        ERROR("%s(): failed to parse payload.#bytes",
              __FUNCTION__);
        free(buf);
        return TE_EBADMSG;
    }

    *id = *(uint64_t *)&buf[i - 8];
    if (htonl(1) != 1)
        *id = bswap_64(*id);

    free(buf);
    return 0;
}

/** CSAP callback for processing captured Tx packets */
static void
process_tx_pkts(asn_value *pkt, void *user_data)
{
    struct timeval ts;
    te_errno rc;

    test_cb_args *args = (test_cb_args *)user_data;

    args->n_pkts++;

    if (args->failed)
        goto cleanup;

    rc = tapi_tad_get_pkt_rx_ts(pkt, &ts);
    if (rc != 0)
    {
        args->failed = TRUE;
        goto cleanup;
    }

    rc = get_pkt_id(pkt, &args->cur_pkt_id);
    if (rc != 0)
    {
        args->failed = TRUE;
        goto cleanup;
    }

    if (args->n_pkts == 1)
    {
        args->prev_ts = ts;
    }
    else
    {
        if (TIMEVAL_SUB(ts, args->prev_ts) > args->pkt_send_delay * 5)
            TE_VEC_APPEND(&args->delayed_pkt_ids, args->cur_pkt_id);

        args->prev_ts = ts;
    }

cleanup:

    asn_free_value(pkt);
}

/** CSAP callback for processing captured Rx packets */
static void
process_rx_pkts(asn_value *pkt, void *user_data)
{
    struct timeval ts;
    te_errno rc;

    test_cb_args *args = (test_cb_args *)user_data;

    args->n_pkts++;

    if (args->failed)
        goto cleanup;

    rc = tapi_tad_get_pkt_rx_ts(pkt, &ts);
    if (rc != 0)
    {
        args->failed = TRUE;
        goto cleanup;
    }

    rc = get_pkt_id(pkt, &args->cur_pkt_id);
    if (rc != 0)
    {
        args->failed = TRUE;
        goto cleanup;
    }

    if (args->n_pkts > 1 && TIMEVAL_SUB(ts, args->prev_ts) > MIN_GROUP_DELAY)
        finish_pkt_group(args);

    args->prev_ts = ts;
    args->cur_group_size++;

cleanup:

    asn_free_value(pkt);
}

/**
 * Finalize computation of packet statistics after CSAP captured
 * the last packet.
 */
static void
finalize_stats(test_cb_args *args)
{
    if (args->n_groups == 0)
    {
        RING("No packet groups with measurable size was detected");
        return;
    }

    args->avg_group_size = (double)args->total_group_size / args->n_groups;
    args->group_size_dev = sqrt(args->squared_dev_sum / args->n_groups);

    RING("Number of packet groups: %u\n"
         "Average group size: %.3f\n"
         "Group size deviation: %.3f",
         args->n_groups, args->avg_group_size, args->group_size_dev);
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

    int coalesce_frames;
    int send_delay;

    uint64_t prev_rx_usecs;
    uint64_t prev_rx_max_frames;
    uint64_t prev_adaptive_rx;
    te_bool restore_prev_values = FALSE;

    csap_handle_t csap_rx = CSAP_INVALID_HANDLE;
    csap_handle_t csap_tx = CSAP_INVALID_HANDLE;
    tapi_tad_trrecv_cb_data csap_cb_data;
    test_cb_args cb_args = TEST_CB_ARGS_INIT;
    int rc_aux;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_INT_PARAM(coalesce_frames);
    TEST_GET_INT_PARAM(send_delay);

    if (send_delay < MIN_SEND_DELAY)
        TEST_FAIL("Requested %u us delay between packets is too small (min is %u us)",
                  send_delay, MIN_SEND_DELAY);

    if (coalesce_frames == 0)
        TEST_FAIL("coalesce_frames must be non zero");

    rc = tapi_cfg_if_coalesce_get(iut_rpcs->ta, iut_if->if_name,
                                  "rx_max_coalesced_frames",
                                  &prev_rx_max_frames);
    if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        TEST_SKIP("Interrupt coalescing settings are not supported");
    else if (rc != 0)
        TEST_FAIL("Failed to get rx_max_coalesced_frames: %r", rc);

    CHECK_RC(tapi_cfg_if_coalesce_get(iut_rpcs->ta, iut_if->if_name,
                                      "rx_coalesce_usecs",
                                      &prev_rx_usecs));
    CHECK_RC(tapi_cfg_if_coalesce_get(iut_rpcs->ta, iut_if->if_name,
                                      "use_adaptive_rx_coalesce",
                                      &prev_adaptive_rx));

    restore_prev_values = TRUE;

    TEST_STEP("Establish connection between a pair of UDP sockets "
              "on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Send a packet from Tester and receive it on IUT to be "
              "sure that IUT IP address is resolved.");
    net_drv_send_recv_check(tst_rpcs, tst_s, iut_rpcs, iut_s,
                            "Sending first packet from Tester");

    TEST_STEP("Disable @b use_adaptive_rx_coalesce on IUT interface.");

    rc = tapi_cfg_if_coalesce_set(iut_rpcs->ta, iut_if->if_name,
                                  "use_adaptive_rx_coalesce", 0);
    if (rc != 0)
        TEST_VERDICT("Failed to set use_adaptive_rx_coalesce, rc=%r", rc);

    TEST_STEP("Set @b rx_coalesce_usecs to @c 0 for the IUT interface. "
              "Set @b rx_max_coalesced_frames to the value of @p coalesce_frames.");

    CHECK_RC(tapi_cfg_if_coalesce_set_local(iut_rpcs->ta, iut_if->if_name,
                                            "rx_coalesce_usecs", 0));
    CHECK_RC(tapi_cfg_if_coalesce_set_local(iut_rpcs->ta, iut_if->if_name,
                                            "rx_max_coalesced_frames",
                                            coalesce_frames));

    rc = tapi_cfg_if_coalesce_commit(iut_rpcs->ta, iut_if->if_name);
    if (TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
    {
        TEST_SUBSTEP("If setting fails with @c EOPNOTSUPP, try to set "
                     "only @b rx_max_coalesced_frames, as setting "
                     "@b rx_coalesce_usecs may be not available.");
        rc = tapi_cfg_if_coalesce_set(iut_rpcs->ta, iut_if->if_name,
                                      "rx_max_coalesced_frames",
                                      coalesce_frames);
        if (TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
        {
            TEST_SKIP("Requested rx_max_coalesced_frames is not supported");
        }
    }
    if (rc != 0)
        TEST_VERDICT("Failed to set rx_max_coalesced_frames, rc=%r", rc);

    TEST_STEP("Warm up Tx on Tester and Rx on IUT (e.g. to wrap Rx ring).");

    tst_rpcs->op = RCF_RPC_CALL;
    rpc_net_drv_send_pkts_exact_delay(tst_rpcs, tst_s, MIN_SEND_DELAY,
                                      WARNUP_TIME_MS);
    rpc_net_drv_recv_pkts_exact_delay(iut_rpcs, iut_s,
                                      TAPI_WAIT_NETWORK_DELAY);
    rpc_net_drv_send_pkts_exact_delay(tst_rpcs, tst_s, MIN_SEND_DELAY,
                                      WARNUP_TIME_MS);

    TEST_STEP("Create a CSAP on Tester to capture packets sent from it "
              "to IUT.");

    CHECK_RC(tapi_udp_ip_eth_csap_create(tst_rpcs->ta, 0, tst_if->if_name,
                                         TAD_ETH_RECV_OUT |
                                         TAD_ETH_RECV_NO_PROMISC,
                                         NULL, NULL,
                                         iut_addr->sa_family,
                                         TAD_SA2ARGS(iut_addr, tst_addr),
                                         &csap_tx));

    CHECK_RC(tapi_tad_trrecv_start(tst_rpcs->ta, 0, csap_tx,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    TEST_STEP("Create a CSAP on IUT to capture packets received from "
              "Tester.");

    CHECK_RC(tapi_udp_ip_eth_csap_create(iut_rpcs->ta, 0, iut_if->if_name,
                                         TAD_ETH_RECV_DEF |
                                         TAD_ETH_RECV_NO_PROMISC,
                                         NULL, NULL,
                                         iut_addr->sa_family,
                                         TAD_SA2ARGS(iut_addr, tst_addr),
                                         &csap_rx));

    CHECK_RC(tapi_tad_trrecv_start(iut_rpcs->ta, 0, csap_rx,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    TEST_STEP("For some time send UDP packets from Tester to IUT, "
              "waiting a little after sending every packet. Receive "
              "and check data on IUT.");

    tst_rpcs->op = RCF_RPC_CALL;
    rpc_net_drv_send_pkts_exact_delay(tst_rpcs, tst_s, send_delay,
                                      SEND_TIME);

    RPC_AWAIT_ERROR(iut_rpcs);
    rc_aux = rpc_net_drv_recv_pkts_exact_delay(iut_rpcs, iut_s,
                                               TAPI_WAIT_NETWORK_DELAY);
    if (rc_aux < 0)
    {
        ERROR_VERDICT("rpc_net_drv_recv_pkts_exact_delay() failed: ",
                      RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    RPC_AWAIT_ERROR(tst_rpcs);
    rc = rpc_net_drv_send_pkts_exact_delay(tst_rpcs, tst_s, send_delay,
                                           SEND_TIME);
    if (rc < 0)
    {
        ERROR_VERDICT("rpc_net_drv_send_pkts_exact_delay() failed: ",
                      RPC_ERROR_FMT, RPC_ERROR_ARGS(tst_rpcs));
    }

    if (rc < 0 || rc_aux < 0)
        TEST_STOP;

    rcf_tr_op_log(FALSE);
    memset(&csap_cb_data, 0, sizeof(csap_cb_data));
    csap_cb_data.callback = &process_tx_pkts;
    csap_cb_data.user_data = &cb_args;

    cb_args.coalesce_frames = coalesce_frames;
    cb_args.pkt_send_delay = send_delay;

    TEST_STEP("Investigate timestamps of Tx packets captured by Tester "
              "CSAP. Memorize numbers of packets sent after big delays "
              "to ignore them when computing coalescing statistics on "
              "IUT.");
    CHECK_RC(tapi_tad_trrecv_stop(tst_rpcs->ta, 0, csap_tx,
                                  &csap_cb_data, NULL));
    RING("%u packets sent from Tester", cb_args.n_pkts);
    if (cb_args.n_pkts == 0)
        TEST_VERDICT("No packets were captured by Tester CSAP");
    if (cb_args.failed)
    {
        TEST_FAIL("Failed to process some of the captured packets "
                  "on Tester");
    }

    TEST_STEP("Investigate timestamps of packets captured by CSAP on IUT. "
              "Packets should be received in groups of @p coalesce_frames "
              "separated by larger delays.");

    cb_args.n_pkts = 0;
    csap_cb_data.callback = &process_rx_pkts;

    CHECK_RC(tapi_tad_trrecv_stop(iut_rpcs->ta, 0, csap_rx,
                                  &csap_cb_data, NULL));
    RING("%u packets captured on IUT", cb_args.n_pkts);
    if (cb_args.n_pkts == 0)
        TEST_VERDICT("No packets were captured by IUT CSAP");
    if (cb_args.failed)
        TEST_FAIL("Failed to process some of the captured packets on IUT");

    finalize_stats(&cb_args);

    if (cb_args.n_groups == 0)
        TEST_FAIL("Failed to get statistics for packet groups");

    if (fabs(cb_args.avg_group_size - coalesce_frames) / coalesce_frames >
                                                      ERR_MARGIN ||
        fabs(cb_args.group_size_dev / coalesce_frames) > ERR_MARGIN)
    {
        TEST_VERDICT("Actual groups of received packets differ "
                     "too much from the expected distribution");
    }

    TEST_SUCCESS;

cleanup:

    if (csap_tx != CSAP_INVALID_HANDLE)
    {
        CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0,
                                               csap_tx));
    }

    if (csap_rx != CSAP_INVALID_HANDLE)
    {
        CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0,
                                               csap_rx));
    }

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    /*
     * Interrupts coalescing configuration should be rolled back manually
     * due to driver quirks which Configurator cannot handle. For instance,
     * on Intel X710 interface, if we have rx-usecs=50 and adaptive-rx=off,
     * trying to restore previous state with
     *
     * ethtool -C enp1s0f0 rx-usecs 2 adaptive-rx on
     *
     * fails with EINVAL, probably because rx-usecs cannot be changed to
     * another specific value while enabling adaptive-rx. It must be done
     * in two steps
     *
     * ethtool -C enp1s0f0 rx-usecs 2
     * ethtool -C enp1s0f0 adaptive-rx on
     *
     * to make Configurator happy with restored configuration state.
     */

    if (restore_prev_values)
    {
        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_set_local(iut_rpcs->ta,
                                                        iut_if->if_name,
                                                        "rx_coalesce_usecs",
                                                        prev_rx_usecs));
        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_set_local(
                                              iut_rpcs->ta, iut_if->if_name,
                                              "rx_max_coalesced_frames",
                                              prev_rx_max_frames));
        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_commit(iut_rpcs->ta,
                                                     iut_if->if_name));

        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_set(
                                              iut_rpcs->ta,
                                              iut_if->if_name,
                                              "use_adaptive_rx_coalesce",
                                              prev_adaptive_rx));
    }

    te_vec_free(&cb_args.delayed_pkt_ids);

    TEST_END;
}
