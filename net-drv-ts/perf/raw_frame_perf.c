/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Performance testing
 */

/**
 * @defgroup perf-raw_frame_perf Ethernet performance test via CSAP/TAD
 * @ingroup perf
 * @{
 *
 * @objective Measure Ethernet performance with CSAP/TAD.
 *
 * @param env               Testing environment:
 *                          - @ref env.peer2peer.iut_server
 *                          - @ref env.peer2peer.iut_client
 *                          - @ref env.peer2peerX2.iut_server
 *                          - @ref env.peer2peerX2.iut_client
 *                          - @ref env.peer2peerX3.iut_server
 *                          - @ref env.peer2peerX3.iut_client
 *                          - @ref env.peer2peerX4.iut_server
 *                          - @ref env.peer2peerX4.iut_client
 * @param frame_size        Ethernet frame size in bytes, including Ethernet.
 *                          header and FCS:
 *                          - @c 64
 *                          - @c 128
 *                          - @c 256
 *                          - @c 512
 *                          - @c 1024
 *                          - @c 1518
 * @param load_duration     Desired load duration in seconds.
 *                          Number of transmitted packets is calculated
 *                          from link speed and this duration.
 * @param max_loss_pct      Maximum acceptable frame loss percent.
 * @param tmpl              Traffic template.
 *
 * @type performance
 *
 * @author Denis Pryazhennikov <denis.pryazhennikov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "perf/raw_frame_perf"

#include "net_drv_test.h"

#include "ndn.h"
#include "tad_common.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_stats.h"
#include "tapi_eth.h"
#include "tapi_ndn.h"
#include "te_defs.h"
#include "te_ethernet.h"
#include "te_mi_log.h"
#include "te_str.h"
#include "te_time.h"
#include "te_units.h"

/* IEEE 802.3 preamble length in bytes. */
#define ETH_PREAMBLE_LEN 7
/* Start Frame Delimiter length in bytes. */
#define ETH_SFD_LEN 1
/* Inter-Packet Gap length in bytes on the wire. */
#define ETH_IPG_LEN 12
/* Per-frame L1 service overhead. */
#define ETH_L1_SERVICE_LEN (ETH_PREAMBLE_LEN + ETH_SFD_LEN + ETH_IPG_LEN)
/* Epsilon for non-zero floating-point checks. */
#define EPS 0.00001
/* Minimal acceptable throughput as a fraction of link speed. */
#define MIN_SPEED_MULTIPLIER 0.01
/* Allowed excess of Rx packets over Tx packets due to background traffic. */
#define RX_TX_PKTS_GAP 10
/* Maximum number of simultaneous links in this test. */
#define TEST_MAX_LINKS 4

typedef struct ts_range {
    struct timeval first;
    struct timeval last;
    struct timeval dur;
} ts_range;

static void
log_summary_mi(double tx_pps, double rx_pps,
               double tx_l1_bps, double rx_l1_bps)
{
    te_mi_logger *logger;

    CHECK_RC(te_mi_logger_meas_create("eth csap perf", &logger));
    te_mi_logger_add_meas_vec(logger, NULL, TE_MI_MEAS_V(
            TE_MI_MEAS(PPS, "Tx", SINGLE, tx_pps, PLAIN),
            TE_MI_MEAS(PPS, "Rx", SINGLE, rx_pps, PLAIN),
            TE_MI_MEAS(THROUGHPUT, "Tx L1", SINGLE, tx_l1_bps, PLAIN),
            TE_MI_MEAS(THROUGHPUT, "Rx L1", SINGLE, rx_l1_bps, PLAIN)));
    te_mi_logger_destroy(logger);
}

static int
get_link_speed(const char *ta, const char *if_name)
{
    int speed = TE_PHY_SPEED_UNKNOWN;
    te_errno rc;

    rc = tapi_cfg_phy_speed_oper_get(ta, if_name, &speed);
    if (rc != 0 || speed == TE_PHY_SPEED_UNKNOWN)
        CHECK_RC(tapi_cfg_phy_speed_admin_get(ta, if_name, &speed));

    if (speed == TE_PHY_SPEED_UNKNOWN)
    {
        TEST_FAIL("Failed to determine valid link speed on %s:%s",
                  ta, if_name);
    }

    return speed;
}

static void
prepare_template(asn_value *tmpl,
                 uint64_t target_packets,
                 int payload_len)
{
    te_errno rc;
    char send_func[RCF_MAX_VAL];

    rc = asn_free_subvalue(tmpl, "arg-sets");
    if (rc != 0 && TE_RC_GET_ERROR(rc) != TE_EASNINCOMPLVAL)
        CHECK_RC(rc);

    rc = asn_free_subvalue(tmpl, "payload");
    if (rc != 0 && TE_RC_GET_ERROR(rc) != TE_EASNINCOMPLVAL)
        CHECK_RC(rc);

    rc = asn_free_subvalue(tmpl, "send-func");
    if (rc != 0 && TE_RC_GET_ERROR(rc) != TE_EASNINCOMPLVAL)
        CHECK_RC(rc);

    CHECK_RC(asn_write_value_field(tmpl, &payload_len, sizeof(payload_len),
                                   "payload.#length"));

    if (target_packets > UINT_MAX)
    {
        TEST_FAIL("Calculated packets number %" PRIu64 " is too high",
                  target_packets);
    }

    CHECK_RC(te_snprintf(send_func, sizeof(send_func),
                         "tad_eth_flood:%" PRIu64, target_packets));
    CHECK_RC(asn_write_string(tmpl, send_func, "send-func"));
}

static uint64_t
if_counter_delta(uint64_t before, uint64_t after, const char *counter,
                 const char *if_name)
{
    if (after < before)
    {
        WARN("Counter %s on %s decreased from %" PRIu64 " to %" PRIu64 "",
             counter, if_name, before, after);
        return 0;
    }

    return after - before;
}

static void
calc_if_stats_delta(const tapi_cfg_if_stats *before,
                    const tapi_cfg_if_stats *after,
                    const char *if_name,
                    tapi_cfg_if_stats *diff)
{
    memset(diff, 0, sizeof(*diff));

#define IF_STATS_DELTA(_field) \
    diff->_field = if_counter_delta(before->_field, after->_field, \
                                    #_field, if_name)

    IF_STATS_DELTA(in_octets);
    IF_STATS_DELTA(in_ucast_pkts);
    IF_STATS_DELTA(in_nucast_pkts);
    IF_STATS_DELTA(in_discards);
    IF_STATS_DELTA(in_errors);
    IF_STATS_DELTA(in_unknown_protos);
    IF_STATS_DELTA(out_octets);
    IF_STATS_DELTA(out_ucast_pkts);
    IF_STATS_DELTA(out_nucast_pkts);
    IF_STATS_DELTA(out_discards);
    IF_STATS_DELTA(out_errors);

#undef IF_STATS_DELTA
}

static void
get_timestamps(const char *ta, csap_handle_t csap, ts_range *ts)
{
    CHECK_RC(tapi_csap_param_get_timestamp(ta, 0, csap,
                                           CSAP_PARAM_FIRST_PACKET_TIME,
                                           &ts->first));
    CHECK_RC(tapi_csap_param_get_timestamp(ta, 0, csap,
                                           CSAP_PARAM_LAST_PACKET_TIME,
                                           &ts->last));
    te_timersub(&ts->last, &ts->first, &ts->dur);
}

static void
set_template_eth_addrs(asn_value *tmpl, const struct sockaddr *dst,
                       const struct sockaddr *src)
{
    CHECK_RC(asn_write_value_field(tmpl, dst->sa_data, ETHER_ADDR_LEN,
                                   "pdus.0.#eth.dst-addr.#plain"));
    CHECK_RC(asn_write_value_field(tmpl, src->sa_data, ETHER_ADDR_LEN,
                                   "pdus.0.#eth.src-addr.#plain"));
}

static void
wait_send_completion(const char *ta, csap_handle_t csap, const char *if_name,
                     unsigned int timeout_s)
{
    tad_csap_status_t status = CSAP_IDLE;
    unsigned int i;

    for (i = 0; i < timeout_s; i++)
    {
        CHECK_RC(tapi_csap_get_status(ta, 0, csap, &status));
        if (status == CSAP_ERROR)
            TEST_FAIL("Tx CSAP reported error on %s", if_name);
        if (status != CSAP_BUSY)
            return;
        VSLEEP(1, "wait for Tx CSAP completion");
    }

    TEST_FAIL("Tx CSAP did not complete on %s within %u seconds",
              if_name, timeout_s);
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *server_rpcs = NULL;
    rcf_rpc_server *client_rpcs = NULL;
    rcf_rpc_server *sender_rpcs = NULL;
    rcf_rpc_server *receiver_rpcs = NULL;
    const struct if_nameindex *server_ifs[TEST_MAX_LINKS] = {};
    const struct if_nameindex *client_ifs[TEST_MAX_LINKS] = {};
    const struct if_nameindex *sender_ifs[TEST_MAX_LINKS] = {};
    const struct if_nameindex *receiver_ifs[TEST_MAX_LINKS] = {};
    struct sockaddr sender_lladdrs[TEST_MAX_LINKS] = {};
    struct sockaddr receiver_lladdrs[TEST_MAX_LINKS] = {};
    unsigned int n_ports = 0;

    int frame_size;
    int load_duration;
    double max_loss_pct;
    asn_value *tmpl = NULL;
    asn_value *tmpls[TEST_MAX_LINKS] = {};

    uint64_t link_speed_bps[TEST_MAX_LINKS] = {};
    uint64_t common_link_speed_bps = 0;
    int payload_len;
    uint64_t pkt_l1_bits;
    uint64_t target_packets[TEST_MAX_LINKS] = {};
    double target_pps[TEST_MAX_LINKS] = {};

    csap_handle_t csap_send[TEST_MAX_LINKS];
    csap_handle_t csap_rx[TEST_MAX_LINKS];

    /* Main metrics are based on interface counters. */
    uint64_t tx_if_pkts[TEST_MAX_LINKS] = {};
    uint64_t rx_if_pkts[TEST_MAX_LINKS] = {};
    unsigned int rx_csap_pkts[TEST_MAX_LINKS] = {};
    uint64_t tx_if_pkts_total = 0;
    uint64_t rx_if_pkts_total = 0;

    /* Interface counter snapshots used to calculate traffic deltas. */
    tapi_cfg_if_stats sender_stats_before[TEST_MAX_LINKS] = {};
    tapi_cfg_if_stats sender_stats_after[TEST_MAX_LINKS] = {};
    tapi_cfg_if_stats sender_stats_diff[TEST_MAX_LINKS] = {};
    tapi_cfg_if_stats receiver_stats_before[TEST_MAX_LINKS] = {};
    tapi_cfg_if_stats receiver_stats_after[TEST_MAX_LINKS] = {};
    tapi_cfg_if_stats receiver_stats_diff[TEST_MAX_LINKS] = {};
    int64_t loss_pkts = 0;
    double loss_pct = 0.0;

    /* Rates are calculated over the sender CSAP active interval. */
    ts_range tx_ts[TEST_MAX_LINKS] = {};
    double tx_time_s[TEST_MAX_LINKS] = {};
    double tx_pps_per_link[TEST_MAX_LINKS] = {};
    double rx_pps_per_link[TEST_MAX_LINKS] = {};
    double tx_l1_bps_per_link[TEST_MAX_LINKS] = {};
    double rx_l1_bps_per_link[TEST_MAX_LINKS] = {};
    double tx_pps = 0.0;
    double rx_pps = 0.0;
    double tx_l1_bps = 0.0;
    double rx_l1_bps = 0.0;
    double total_line_rate_bps = 0.0;
    double tx_util_pct = 0.0;
    double rx_util_pct = 0.0;
    unsigned int send_timeout_s = 0;
    bool iut_is_sender = false;
    te_string str = TE_STRING_INIT;
    unsigned int i;

    for (i = 0; i < TE_ARRAY_LEN(csap_send); i++)
    {
        csap_send[i] = CSAP_INVALID_HANDLE;
        csap_rx[i] = CSAP_INVALID_HANDLE;
    }

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(server_rpcs);
    TEST_GET_PCO(client_rpcs);
    TEST_GET_INT_PARAM(frame_size);
    TEST_GET_INT_PARAM(load_duration);
    TEST_GET_DOUBLE_PARAM(max_loss_pct);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);

    sender_rpcs = client_rpcs;
    receiver_rpcs = server_rpcs;
    iut_is_sender = strcmp(sender_rpcs->ta, iut_rpcs->ta) == 0;
    CHECK_RC(tapi_ndn_subst_env(tmpl, NULL, &env));

    for (i = 0; i < TEST_MAX_LINKS; i++)
    {
        te_string_reset(&str);
        te_string_append(&str, "server_if%u", i);
        server_ifs[i] = tapi_env_get_if(&env, te_string_value(&str));
        if (server_ifs[i] == NULL)
            break;

        te_string_reset(&str);
        te_string_append(&str, "client_if%u", i);
        client_ifs[i] = tapi_env_get_if(&env, te_string_value(&str));
        CHECK_NOT_NULL(client_ifs[i]);

        sender_ifs[i] = client_ifs[i];
        receiver_ifs[i] = server_ifs[i];

        CHECK_RC(tapi_cfg_base_if_get_link_addr(sender_rpcs->ta,
                                                sender_ifs[i]->if_name,
                                                &sender_lladdrs[i]));
        CHECK_RC(tapi_cfg_base_if_get_link_addr(receiver_rpcs->ta,
                                                receiver_ifs[i]->if_name,
                                                &receiver_lladdrs[i]));
        n_ports++;
    }

    if (n_ports == 0)
        TEST_FAIL("No server/client interface pairs found in env");

    payload_len = frame_size - ETHER_HDR_LEN - ETHER_CRC_LEN;
    if (payload_len <= 0)
    {
        TEST_FAIL("frame_size %d is too small for Ethernet overhead",
                  frame_size);
    }

    pkt_l1_bits = (uint64_t)(frame_size + ETH_L1_SERVICE_LEN) * CHAR_BIT;
    send_timeout_s = (unsigned int)((double)load_duration /
                                    MIN_SPEED_MULTIPLIER) + 30;

    for (i = 0; i < n_ports; i++)
    {
        const struct if_nameindex *iut_if = iut_is_sender ?
                                            sender_ifs[i] : receiver_ifs[i];

        link_speed_bps[i] = (uint64_t)TE_UNITS_DEC_M2U(
                                get_link_speed(iut_rpcs->ta, iut_if->if_name));
        if (i == 0)
            common_link_speed_bps = link_speed_bps[i];

        target_packets[i] = TE_DIV_ROUND_UP(
                                link_speed_bps[i] * (uint64_t)load_duration,
                                pkt_l1_bits);
        target_pps[i] = (double)link_speed_bps[i] / pkt_l1_bits;

        tmpls[i] = asn_copy_value(tmpl);
        if (tmpls[i] == NULL)
            TEST_VERDICT("Failed to copy traffic template");

        prepare_template(tmpls[i], target_packets[i], payload_len);
        set_template_eth_addrs(tmpls[i], &receiver_lladdrs[i],
                               &sender_lladdrs[i]);

        CHECK_RC(tapi_eth_based_csap_create_by_tmpl(
                     sender_rpcs->ta, 0, sender_ifs[i]->if_name,
                     TAD_ETH_RECV_NO, tmpls[i], &csap_send[i]));

        CHECK_RC(tapi_eth_based_csap_create_by_tmpl(
                     receiver_rpcs->ta, 0, receiver_ifs[i]->if_name,
                     TAD_ETH_RECV_DEF | TAD_ETH_RECV_NO_PROMISC,
                     tmpls[i], &csap_rx[i]));
    }

    for (i = 0; i < n_ports; i++)
    {
        CHECK_RC(tapi_tad_trrecv_start(receiver_rpcs->ta, 0, csap_rx[i], NULL,
                                       TAD_TIMEOUT_INF, 0, RCF_TRRECV_COUNT));
    }

    CHECK_RC(tapi_env_stats_gather(&env));
    for (i = 0; i < n_ports; i++)
    {
        CHECK_RC(tapi_cfg_stats_if_stats_get(sender_rpcs->ta,
                                             sender_ifs[i]->if_name,
                                             &sender_stats_before[i]));
        CHECK_RC(tapi_cfg_stats_if_stats_get(receiver_rpcs->ta,
                                             receiver_ifs[i]->if_name,
                                             &receiver_stats_before[i]));
    }

    TEST_STEP("Send Ethernet frames on all links simultaneously.");
    for (i = 0; i < n_ports; i++)
    {
        CHECK_RC(tapi_tad_trsend_start(sender_rpcs->ta, 0, csap_send[i],
                                       tmpls[i], RCF_MODE_NONBLOCKING));
    }

    for (i = 0; i < n_ports; i++)
    {
        wait_send_completion(sender_rpcs->ta, csap_send[i],
                             sender_ifs[i]->if_name,
                             send_timeout_s);
    }

    TAPI_WAIT_NETWORK;

    for (i = 0; i < n_ports; i++)
    {
        CHECK_RC(tapi_tad_trrecv_stop(receiver_rpcs->ta, 0, csap_rx[i], NULL,
                                      &rx_csap_pkts[i]));

        if (rx_csap_pkts[i] > target_packets[i])
        {
            uint64_t rx_tx_gap = rx_csap_pkts[i] - target_packets[i];

            if (rx_tx_gap > RX_TX_PKTS_GAP)
            {
                ERROR("Rx CSAP captured more packets than were requested on link #%u "
                      "(%u > %" PRIu64 ", gap %" PRIu64 " > %u)",
                      i, rx_csap_pkts[i], target_packets[i], rx_tx_gap,
                      RX_TX_PKTS_GAP);
                ERROR_VERDICT("Rx CSAP captured more packets than were requested");
            }

            rx_csap_pkts[i] = (unsigned int)target_packets[i];
        }
    }

    NET_DRV_WAIT_IF_STATS_UPDATE;
    for (i = 0; i < n_ports; i++)
    {
        CHECK_RC(tapi_cfg_stats_if_stats_get(sender_rpcs->ta,
                                             sender_ifs[i]->if_name,
                                             &sender_stats_after[i]));
        CHECK_RC(tapi_cfg_stats_if_stats_get(receiver_rpcs->ta,
                                             receiver_ifs[i]->if_name,
                                             &receiver_stats_after[i]));

        calc_if_stats_delta(&sender_stats_before[i], &sender_stats_after[i],
                            sender_ifs[i]->if_name, &sender_stats_diff[i]);
        calc_if_stats_delta(&receiver_stats_before[i], &receiver_stats_after[i],
                            receiver_ifs[i]->if_name,
                            &receiver_stats_diff[i]);

        tx_if_pkts[i] = sender_stats_diff[i].out_ucast_pkts;
        /*
         * Treat packets counted by Rx NIC as received by the link even if
         * they were dropped later due to local resource pressure.
         */
        rx_if_pkts[i] = receiver_stats_diff[i].in_ucast_pkts +
                        receiver_stats_diff[i].in_discards;

        if (rx_if_pkts[i] > tx_if_pkts[i])
        {
            uint64_t rx_tx_gap = rx_if_pkts[i] - tx_if_pkts[i];

            if (rx_tx_gap > RX_TX_PKTS_GAP)
            {
                ERROR("Rx interface counted more packets than Tx interface "
                      "on link #%u (%" PRIu64 " > %" PRIu64 ", "
                      "gap %" PRIu64 " > %u)",
                      i, rx_if_pkts[i], tx_if_pkts[i], rx_tx_gap,
                      RX_TX_PKTS_GAP);
                ERROR_VERDICT("Rx interface counted more packets than Tx interface");
            }

            rx_if_pkts[i] = tx_if_pkts[i];
        }

        tx_if_pkts_total += tx_if_pkts[i];
        rx_if_pkts_total += rx_if_pkts[i];
    }

    for (i = 0; i < n_ports; i++)
    {
        get_timestamps(sender_rpcs->ta, csap_send[i], &tx_ts[i]);
        tx_time_s[i] = tx_ts[i].dur.tv_sec +
                       (double)tx_ts[i].dur.tv_usec / TE_SEC2US(1);

        if (tx_time_s[i] > 0.0)
        {
            tx_pps_per_link[i] = tx_if_pkts[i] / tx_time_s[i];
            tx_l1_bps_per_link[i] = tx_pps_per_link[i] * pkt_l1_bits;
            tx_pps += tx_pps_per_link[i];
            tx_l1_bps += tx_l1_bps_per_link[i];

            rx_pps_per_link[i] = rx_if_pkts[i] / tx_time_s[i];
            rx_l1_bps_per_link[i] = rx_pps_per_link[i] * pkt_l1_bits;
            rx_pps += rx_pps_per_link[i];
            rx_l1_bps += rx_l1_bps_per_link[i];
        }
    }

    loss_pkts = (int64_t)tx_if_pkts_total - (int64_t)rx_if_pkts_total;
    if (tx_if_pkts_total > 0)
        loss_pct = (double)loss_pkts * 100.0 / tx_if_pkts_total;

    total_line_rate_bps = common_link_speed_bps * n_ports;

    if (total_line_rate_bps > EPS)
    {
        tx_util_pct = tx_l1_bps * 100.0 / total_line_rate_bps;
        rx_util_pct = rx_l1_bps * 100.0 / total_line_rate_bps;
    }

    TEST_ARTIFACT("PHY: %u links x %.2f Mbps, target %.3f Mpps/link, "
                  "min checked L1 %.2f Mbps/link",
                  n_ports, TE_UNITS_DEC_U2M(common_link_speed_bps),
                  TE_UNITS_DEC_U2M(target_pps[0]),
                  TE_UNITS_DEC_U2M(common_link_speed_bps *
                                   MIN_SPEED_MULTIPLIER));
    TEST_ARTIFACT("Aggregate: Tx %.2f Mbps (%.3f Mpps, %.1f%%), "
                  "Rx %.2f Mbps (%.3f Mpps, %.1f%%), "
                  "loss %.3f Mpkts (%.3f%%)",
                  TE_UNITS_DEC_U2M(tx_l1_bps),
                  TE_UNITS_DEC_U2M(tx_pps), tx_util_pct,
                  TE_UNITS_DEC_U2M(rx_l1_bps),
                  TE_UNITS_DEC_U2M(rx_pps), rx_util_pct,
                  TE_UNITS_DEC_U2M(loss_pkts), loss_pct);
    RING("Aggregate packets: Tx %.3f Mpkts, Rx %.3f Mpkts, "
         "loss %.3f Mpkts",
         TE_UNITS_DEC_U2M(tx_if_pkts_total),
         TE_UNITS_DEC_U2M(rx_if_pkts_total),
         TE_UNITS_DEC_U2M(loss_pkts));

    for (i = 0; i < n_ports; i++)
    {
        int64_t loss_pkts_link = (int64_t)tx_if_pkts[i] -
                                  (int64_t)rx_if_pkts[i];
        double loss_pct_link = tx_if_pkts[i] > 0 ?
                               (double)loss_pkts_link * 100.0 /
                               tx_if_pkts[i] : 0.0;
        double min_l1_bps = (double)link_speed_bps[i] * MIN_SPEED_MULTIPLIER;
        double tx_util_pct_link = link_speed_bps[i] > 0 ?
                                  tx_l1_bps_per_link[i] * 100.0 /
                                  link_speed_bps[i] : 0.0;
        double rx_util_pct_link = link_speed_bps[i] > 0 ?
                                  rx_l1_bps_per_link[i] * 100.0 /
                                  link_speed_bps[i] : 0.0;
        double checked_l1_bps = iut_is_sender ? rx_l1_bps_per_link[i] :
                                                tx_l1_bps_per_link[i];
        const char *checked_dir = iut_is_sender ? "IUT->Tester" :
                                                  "Tester->IUT";
        const char *checked_iface = iut_is_sender ?
                                    sender_ifs[i]->if_name :
                                    receiver_ifs[i]->if_name;

        TEST_ARTIFACT("Link #%u %s -> %s: Tx/Rx %.2f/%.2f Mbps "
                      "(%.1f/%.1f%%), loss %.3f%%",
                      i, sender_ifs[i]->if_name, receiver_ifs[i]->if_name,
                      TE_UNITS_DEC_U2M(tx_l1_bps_per_link[i]),
                      TE_UNITS_DEC_U2M(rx_l1_bps_per_link[i]),
                      tx_util_pct_link, rx_util_pct_link, loss_pct_link);
        RING("Link #%u packets: Tx %.3f Mpkts, Rx %.3f Mpkts, "
             "loss %.3f Mpkts",
             i, TE_UNITS_DEC_U2M(tx_if_pkts[i]),
             TE_UNITS_DEC_U2M(rx_if_pkts[i]),
             TE_UNITS_DEC_U2M(loss_pkts_link));
        RING("Link #%u packet rate: Tx %.3f Mpps, Rx %.3f Mpps",
             i, TE_UNITS_DEC_U2M(tx_pps_per_link[i]),
             TE_UNITS_DEC_U2M(rx_pps_per_link[i]));

        if (loss_pct_link > max_loss_pct)
        {
            ERROR("Frame loss on link #%u (%.3f%%) exceeds max_loss_pct %.3f%%",
                  i, loss_pct_link, max_loss_pct);
        }

        if (checked_l1_bps < EPS)
        {
            ERROR("Zero throughput in %s direction for link #%u (IUT interface: %s)",
                  checked_dir, i, checked_iface);
            TEST_VERDICT("Zero IUT throughput in checked direction");
        }
        else if (checked_l1_bps < min_l1_bps)
        {
            ERROR("Throughput is too low in %s direction for link #%u "
                  "(IUT interface: %s, L1 %.2f Mbps < %.2f Mbps threshold)",
                  checked_dir, i, checked_iface,
                  TE_UNITS_DEC_U2M(checked_l1_bps),
                  TE_UNITS_DEC_U2M(min_l1_bps));
            TEST_VERDICT("IUT throughput is too low in checked direction");
        }
    }

    log_summary_mi(tx_pps, rx_pps, tx_l1_bps, rx_l1_bps);

    if (loss_pct > max_loss_pct)
    {
        ERROR("Frame loss %.3f%% exceeds max_loss_pct %.3f%%",
              loss_pct, max_loss_pct);
        ERROR_VERDICT("Frame loss exceeds configured threshold");
    }

    TEST_SUCCESS;

cleanup:
    CLEANUP_CHECK_RC(tapi_tad_csap_destroy_all(0));

    for (i = 0; i < TE_ARRAY_LEN(tmpls); i++)
        asn_free_value(tmpls[i]);
    asn_free_value(tmpl);
    te_string_free(&str);

    CLEANUP_CHECK_RC(tapi_env_stats_gather_and_log_diff(&env));

    TEST_END;
}

/** @} */
