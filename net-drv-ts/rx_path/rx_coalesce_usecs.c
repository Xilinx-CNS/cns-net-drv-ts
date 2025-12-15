/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Rx path tests
 */

/**
 * @defgroup rx_path-rx_coalesce_usecs Setting rx_coalesce_usecs to a specific value
 * @ingroup rx_path
 * @{
 *
 * @objective Check that when @b rx_coalesce_usecs is set to a specific value,
 *            Rx interrupts coalescing works accordingly.
 *
 * @param env             Testing environment:
 *                        - @ref env-peer2peer
 *                        - @ref env-peer2peer_ipv6
 * @param coalesce_usecs  Value to set for @b rx_coalesce_usecs:
 *                        - @c 0 (interrupt moderation is disabled)
 *                        - @c 300
 *                        - @c 800
 * @param per_queue       If TRUE, test per queue interrupt coalescing
 *                        configuration.
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "rx_path/rx_coalesce_usecs"

#include "net_drv_test.h"
#include "tapi_eth.h"
#include "tapi_udp.h"
#include "tapi_cfg_if_coalesce.h"
#include "tapi_cfg_rx_rule.h"
#include "tapi_cfg_if_chan.h"
#include "tapi_mem.h"

#include <math.h>
#include <byteswap.h>

/**
 * Minimum delay between packets after which the next packet
 * is considered to be from another packets group, microseconds.
 */
static unsigned int min_group_delay = 100;

/**
 * Minimum value for min_group_delay, microseconds.
 */
#define MIN_GROUP_DELAY_LOW 30

/**
 * Minimum nonzero value of rx_coalesce_usecs which this test can
 * check reliably.
 */
#define MIN_NONZERO_USECS (MIN_GROUP_DELAY_LOW * 10)

/**
 * Maximum relative deviation of actual packet distrubition
 * parameter value from expected one.
 */
#define ERR_MARGIN 0.10

/**
 * Maximum share of unexpectedly large delays between packets when
 * rx_coalesce_usecs is zero.
 */
#define MAX_UNEXP_DELAYS 0.005

/** Minimum delay between packet sends on Tester, in microseconds. */
#define MIN_SEND_DELAY 10

/**
 * Choose sending delay so that no more than this number of packets
 * should be coalesced.
 */
#define MAX_COALESCED_PKTS 30

/** How long to send packets from Tester, in milliseconds */
#define SEND_TIME 2000

/** Data passed to and from CSAP callback */
typedef struct test_cb_args {
    /** Value of rx_coalesce_usecs */
    unsigned int coalesce_usecs;
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
    /** Timestamp of the first packet in the current packet group */
    struct timeval cur_group_ts;

    /** Number of captured packets */
    unsigned int n_pkts;
    /** Id of the last captured packet */
    uint64_t cur_pkt_id;
    /** Number of packet groups */
    unsigned int n_groups;
    /** Total duration of all Rx packet groups, in us */
    double total_groups_time;
    /**
     * Sum of squared deviations of actual packet group durations
     * from rx_coalesce_usecs
     */
    double squared_dev_sum;
    /** Average duration of a packet group */
    double avg_group_time;
    /**
     * Square root of average squared deviation of packet group
     * duration from rx_coalesce_usecs
     */
    double group_time_dev;

    /** Will be set to TRUE if packet processing failed */
    te_bool failed;
} test_cb_args;

/** Initializer for test_cb_args */
#define TEST_CB_ARGS_INIT \
    {                                               \
      .coalesce_usecs = 0,                          \
      .pkt_send_delay = 0,                          \
      .delayed_pkt_ids = TE_VEC_INIT(uint64_t),     \
      .cur_delayed_idx = 0,                         \
      .prev_ts = { 0, },                            \
      .cur_group_ts = { 0, },                       \
      .n_pkts = 0,                                  \
      .cur_pkt_id = 0,                              \
      .n_groups = 0,                                \
      .total_groups_time = 0.0,                     \
      .squared_dev_sum = 0.0,                       \
      .avg_group_time = 0.0,                        \
      .group_time_dev = 0.0,                        \
      .failed = FALSE,                              \
    }

/**
 * Finalize the current packet group, update related
 * statistics.
 */
static void
finish_pkt_group(test_cb_args *args, struct timeval *ts)
{
    double group_duration;
    double duration_deviation;
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
             * delay, it is hard to estimate real duration of the
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

        group_duration = TIMEVAL_SUB(*ts, args->cur_group_ts);
        args->total_groups_time += group_duration;
        duration_deviation = group_duration - args->coalesce_usecs;
        args->squared_dev_sum += duration_deviation * duration_deviation;
    }

    args->cur_group_ts = *ts;
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

    if (args->n_pkts == 1)
    {
        args->prev_ts = ts;
        args->cur_group_ts = ts;
    }
    else
    {
        if (TIMEVAL_SUB(ts, args->prev_ts) > min_group_delay)
            finish_pkt_group(args, &ts);

        args->prev_ts = ts;
    }

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
        RING("No packet groups with measurable duration was detected");
        return;
    }

    args->avg_group_time = args->total_groups_time / args->n_groups;
    args->group_time_dev = sqrt(args->squared_dev_sum / args->n_groups);

    RING("Number of packet groups: %u\n"
         "Average group time: %.3f microseconds\n"
         "Group time deviation: %.3f",
         args->n_groups, args->avg_group_time, args->group_time_dev);
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

    bool per_queue = false;
    int coalesce_usecs;
    int max_frames;
    int send_delay;

    uint64_t prev_rx_usecs;
    uint64_t prev_rx_max_frames;
    uint64_t prev_adaptive_rx;
    bool restore_prev_values = false;
    bool remove_rule = false;

    csap_handle_t csap_rx = CSAP_INVALID_HANDLE;
    csap_handle_t csap_tx = CSAP_INVALID_HANDLE;
    tapi_tad_trrecv_cb_data csap_cb_data;
    test_cb_args cb_args = TEST_CB_ARGS_INIT;
    int rc_aux;

    tapi_cfg_rx_rule_flow flow_type;
    int64_t location = TAPI_CFG_RX_RULE_ANY;
    int queue_id = -1;
    int combined_num = 0;
    int rx_num = 0;
    int tx_num = 0;
    int queues_num = 0;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_INT_PARAM(coalesce_usecs);
    TEST_GET_BOOL_PARAM(per_queue);

    if (per_queue)
    {
        CHECK_RC(tapi_cfg_if_chan_cur_get(iut_rpcs->ta, iut_if->if_name,
                                          TAPI_CFG_IF_CHAN_COMBINED,
                                          &combined_num));
        CHECK_RC(tapi_cfg_if_chan_cur_get(iut_rpcs->ta, iut_if->if_name,
                                          TAPI_CFG_IF_CHAN_RX,
                                          &rx_num));
        CHECK_RC(tapi_cfg_if_chan_cur_get(iut_rpcs->ta, iut_if->if_name,
                                          TAPI_CFG_IF_CHAN_TX,
                                          &tx_num));
        queues_num = MAX(rx_num, tx_num) + combined_num;
        if (queues_num == 0)
            TEST_SKIP("No interface queues");

        queue_id = rand_range(0, queues_num - 1);
        RING("Testing queue %d", queue_id);

        TEST_STEP("If @p per_queue is TRUE, configure a rule to use a "
                  "specific queue to process tested traffic. Interrupt "
                  "coalescing then is configured for this specific queue "
                  "rather than for the whole interface.");

        flow_type = tapi_cfg_rx_rule_flow_by_socket(iut_addr->sa_family,
                                                    RPC_SOCK_DGRAM);

        CHECK_RC(tapi_cfg_rx_rule_find_location(iut_rpcs->ta, iut_if->if_name,
                                                0, 0, &location));

        CHECK_RC(tapi_cfg_rx_rule_add(iut_rpcs->ta, iut_if->if_name,
                                      location, flow_type));

        CHECK_RC(tapi_cfg_rx_rule_fill_ip_addrs_ports(
                                           iut_rpcs->ta, iut_if->if_name,
                                           location,
                                           iut_addr->sa_family,
                                           tst_addr, NULL,
                                           iut_addr, NULL));

        CHECK_RC(tapi_cfg_rx_rule_rx_queue_set(iut_rpcs->ta, iut_if->if_name,
                                               location, queue_id));

        rc = tapi_cfg_rx_rule_commit(iut_rpcs->ta, iut_if->if_name,
                                     location);
        if (rc != 0)
            TEST_VERDICT("Failed to add Rx rule: %r", rc);

        remove_rule = true;
    }

    if (coalesce_usecs > 0 &&
        coalesce_usecs < MIN_NONZERO_USECS)
    {
        TEST_FAIL("Too small value of coalesce_usecs is requested for "
                  "this test to work");
    }

    min_group_delay = MIN(min_group_delay, coalesce_usecs / 10);
    min_group_delay = MAX(min_group_delay, MIN_GROUP_DELAY_LOW);

    send_delay = MAX(MIN_SEND_DELAY, coalesce_usecs / MAX_COALESCED_PKTS);
    RING("%u us delay between packets is chosen", send_delay);

    /*
     * In ethtool.h it is said that it is illegal to set both
     * rx_coalesce_usecs and rx_max_coalesced_frames to zero.
     */
    if (coalesce_usecs == 0)
        max_frames = 1;
    else
        max_frames = 0;

    rc = tapi_cfg_if_coalesce_queue_get(iut_rpcs->ta, iut_if->if_name,
                                        queue_id, "rx_coalesce_usecs",
                                        &prev_rx_usecs);
    if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        TEST_SKIP("Interrupt coalescing settings are not supported");
    else if (rc != 0)
        TEST_FAIL("Failed to get rx_coalesce_usecs: %r", rc);

    CHECK_RC(tapi_cfg_if_coalesce_queue_get(iut_rpcs->ta, iut_if->if_name,
                                            queue_id,
                                            "rx_max_coalesced_frames",
                                            &prev_rx_max_frames));
    CHECK_RC(tapi_cfg_if_coalesce_queue_get(iut_rpcs->ta, iut_if->if_name,
                                            queue_id,
                                            "use_adaptive_rx_coalesce",
                                            &prev_adaptive_rx));

    restore_prev_values = true;

    TEST_STEP("Establish connection between a pair of UDP sockets "
              "on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Send a packet from Tester and receive it on IUT to be "
              "sure that IUT IP address is resolved.");
    net_drv_send_recv_check(tst_rpcs, tst_s, iut_rpcs, iut_s,
                            "Sending first packet from Tester");

    TEST_STEP("Disable @b use_adaptive_rx_coalesce on IUT interface.");

    rc = tapi_cfg_if_coalesce_queue_set(iut_rpcs->ta, iut_if->if_name,
                                        queue_id,
                                        "use_adaptive_rx_coalesce", 0);
    if (rc != 0)
        TEST_VERDICT("Failed to set use_adaptive_rx_coalesce, rc=%r", rc);

    TEST_STEP("Set @b rx_coalesce_usecs to the value of @p coalesce_usecs "
              "for the IUT interface or its queue. Also for nonzero "
              "@p coalesce_usecs set @b rx_max_coalesced_frames to zero "
              "so that it is not taken into account when coalescing; for "
              "zero @p coalesce_usecs set it to @c 1.");

    CHECK_RC(tapi_cfg_if_coalesce_queue_set_local(iut_rpcs->ta, iut_if->if_name,
                                                  queue_id, "rx_coalesce_usecs",
                                                  coalesce_usecs));
    CHECK_RC(tapi_cfg_if_coalesce_queue_set_local(iut_rpcs->ta, iut_if->if_name,
                                                  queue_id,
                                                  "rx_max_coalesced_frames",
                                                  max_frames));

    rc = tapi_cfg_if_coalesce_queues_commit(iut_rpcs->ta, iut_if->if_name,
                                            queue_id >= 0);
    if (TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
    {
        TEST_SUBSTEP("If setting fails with @c EOPNOTSUPP, try to set "
                     "only @b rx_coalesce_usecs, as setting "
                     "@b rx_max_coalesced_frames may be not available.");
        rc = tapi_cfg_if_coalesce_queue_set(iut_rpcs->ta, iut_if->if_name,
                                            queue_id, "rx_coalesce_usecs",
                                            coalesce_usecs);
        if (TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
        {
            TEST_SKIP("rx_coalesce_usecs=%u is not supported",
                      coalesce_usecs);
        }
    }
    if (rc != 0)
        TEST_VERDICT("Failed to set rx_coalesce_usecs, rc=%r", rc);

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

    cb_args.coalesce_usecs = coalesce_usecs;
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
              "If @p coalesce_usecs is @c 0, there should be only small "
              "delays after every packet. If @p coalesce_usecs is "
              "not zero, packets should be received in groups separated "
              "by larger delays.");

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
    if (coalesce_usecs > 0)
    {
        if (cb_args.n_groups == 0)
            TEST_FAIL("Failed to get statistics for packet groups");

        if (fabs(cb_args.avg_group_time - coalesce_usecs) / coalesce_usecs >
                                                          ERR_MARGIN ||
            fabs(cb_args.group_time_dev / coalesce_usecs) > ERR_MARGIN)
        {
            TEST_VERDICT("Actual timestamps of received packets differ "
                         "too much from the expected distribution");
        }
    }
    else
    {
        if ((double)(cb_args.n_groups) / cb_args.n_pkts > MAX_UNEXP_DELAYS)
            TEST_VERDICT("Too many big delays between packets");
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
        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_queue_set_local(
                                                        iut_rpcs->ta,
                                                        iut_if->if_name,
                                                        queue_id,
                                                        "rx_coalesce_usecs",
                                                        prev_rx_usecs));
        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_queue_set_local(
                                              iut_rpcs->ta, iut_if->if_name,
                                              queue_id,
                                              "rx_max_coalesced_frames",
                                              prev_rx_max_frames));

        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_queues_commit(
                                                    iut_rpcs->ta,
                                                    iut_if->if_name,
                                                    queue_id >= 0));

        CLEANUP_CHECK_RC(tapi_cfg_if_coalesce_queue_set(
                                              iut_rpcs->ta,
                                              iut_if->if_name,
                                              queue_id,
                                              "use_adaptive_rx_coalesce",
                                              prev_adaptive_rx));
    }

    if (remove_rule)
    {
        CLEANUP_CHECK_RC(tapi_cfg_rx_rule_del(iut_rpcs->ta, iut_if->if_name,
                                              location));
    }

    te_vec_free(&cb_args.delayed_pkt_ids);

    TEST_END;
}
