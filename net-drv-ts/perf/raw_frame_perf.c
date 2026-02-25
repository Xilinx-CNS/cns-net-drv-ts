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

typedef struct ts_range {
    struct timeval first;
    struct timeval last;
    struct timeval dur;
} ts_range;

static void
add_pps_artifacts(double tx_pps, double rx_pps)
{
    if (rx_pps > EPS && tx_pps > EPS)
    {
        TEST_ARTIFACT("Packet rate: Tx: %.3f pps, Rx: %.3f pps",
                      tx_pps, rx_pps);
        return;
    }
    else if (rx_pps > EPS)
    {
        TEST_ARTIFACT("Packet rate: Rx: %.3f pps", rx_pps);
        return;
    }
    else if (tx_pps > EPS)
    {
        TEST_ARTIFACT("Packet rate: Tx: %.3f pps", tx_pps);
    }
}

static void
add_l1_bitrate_artifacts(double tx_l1_bps, double rx_l1_bps)
{
    if (rx_l1_bps > EPS && tx_l1_bps > EPS)
    {
        TEST_ARTIFACT("L1 bitrate: Tx: %.2f Mbps, Rx: %.2f Mbps",
                      TE_UNITS_DEC_U2M(tx_l1_bps),
                      TE_UNITS_DEC_U2M(rx_l1_bps));
        return;
    }
    else if (rx_l1_bps > EPS)
    {
        TEST_ARTIFACT("L1 bitrate: Rx: %.2f Mbps",
                      TE_UNITS_DEC_U2M(rx_l1_bps));
        return;
    }
    else if (tx_l1_bps > EPS)
    {
        TEST_ARTIFACT("L1 bitrate: Tx: %.2f Mbps",
                      TE_UNITS_DEC_U2M(tx_l1_bps));
    }
}

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

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *server_rpcs = NULL;
    rcf_rpc_server *client_rpcs = NULL;
    rcf_rpc_server *sender_rpcs = NULL;
    rcf_rpc_server *receiver_rpcs = NULL;
    const struct if_nameindex *server_if0 = NULL;
    const struct if_nameindex *client_if0 = NULL;
    const struct if_nameindex *sender_iface = NULL;
    const struct if_nameindex *receiver_iface = NULL;

    int frame_size;
    int load_duration;
    double max_loss_pct;
    asn_value *tmpl = NULL;

    int link_speed_mbps;
    uint64_t link_speed_bps;
    int payload_len;
    uint64_t pkt_l1_bits;
    uint64_t target_packets;
    double target_pps;

    csap_handle_t csap_send = CSAP_INVALID_HANDLE;
    csap_handle_t csap_rx = CSAP_INVALID_HANDLE;

    unsigned int tx_pkts = 0;
    unsigned int rx_pkts = 0;
    int64_t loss_pkts = 0;
    double loss_pct = 0.0;

    ts_range tx_ts = { 0 };
    ts_range rx_ts = { 0 };
    double tx_time_s = 0.0;
    double rx_time_s = 0.0;
    double tx_pps = 0.0;
    double rx_pps = 0.0;
    double tx_l1_bps = 0.0;
    double rx_l1_bps = 0.0;
    double min_l1_bps = 0.0;
    double checked_l1_bps = 0.0;
    bool iut_is_sender = false;
    const char *checked_dir = NULL;
    const char *checked_iface = NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(server_rpcs);
    TEST_GET_PCO(client_rpcs);
    TEST_GET_IF(server_if0);
    TEST_GET_IF(client_if0);
    TEST_GET_INT_PARAM(frame_size);
    TEST_GET_INT_PARAM(load_duration);
    TEST_GET_DOUBLE_PARAM(max_loss_pct);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);

    sender_rpcs = client_rpcs;
    sender_iface = client_if0;
    receiver_rpcs = server_rpcs;
    receiver_iface = server_if0;
    iut_is_sender = strcmp(sender_rpcs->ta, iut_rpcs->ta) == 0;
    CHECK_RC(tapi_ndn_subst_env(tmpl, NULL, &env));

    payload_len = frame_size - ETHER_HDR_LEN - ETHER_CRC_LEN;
    if (payload_len <= 0)
    {
        TEST_FAIL("frame_size %d is too small for Ethernet overhead",
                  frame_size);
    }

    link_speed_mbps = get_link_speed(
        iut_is_sender ? sender_rpcs->ta : receiver_rpcs->ta,
        iut_is_sender ? sender_iface->if_name : receiver_iface->if_name);

    link_speed_bps = (uint64_t)TE_UNITS_DEC_M2U(link_speed_mbps);

    pkt_l1_bits = (uint64_t)(frame_size + ETH_L1_SERVICE_LEN) * CHAR_BIT;
    target_packets = TE_DIV_ROUND_UP(
                         link_speed_bps * (uint64_t)load_duration,
                         pkt_l1_bits);
    target_pps = (double)link_speed_bps / pkt_l1_bits;

    prepare_template(tmpl, target_packets, payload_len);

    TEST_STEP("Create sender CSAP for frame generation by template.");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(
                 sender_rpcs->ta, 0, sender_iface->if_name, TAD_ETH_RECV_NO,
                 tmpl, &csap_send));

    TEST_STEP("Create Rx capture CSAP on receiver.");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(
                 receiver_rpcs->ta, 0, receiver_iface->if_name,
                 TAD_ETH_RECV_DEF | TAD_ETH_RECV_NO_PROMISC, tmpl, &csap_rx));

    CHECK_RC(tapi_tad_trrecv_start(receiver_rpcs->ta, 0, csap_rx,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_COUNT));

    CHECK_RC(tapi_env_stats_gather(&env));

    TEST_STEP("Send Ethernet frames from sender CSAP.");
    CHECK_RC(tapi_tad_trsend_start(sender_rpcs->ta, 0, csap_send,
                                   tmpl, RCF_MODE_BLOCKING));
    /*
     * In blocking mode successful trsend_start() completes only after the
     * whole requested template has been sent.
     */
    tx_pkts = (unsigned int)target_packets;

    TAPI_WAIT_NETWORK;

    CHECK_RC(tapi_tad_trrecv_stop(receiver_rpcs->ta, 0, csap_rx,
                                  NULL, &rx_pkts));

    if (rx_pkts > tx_pkts)
    {
        unsigned int rx_tx_gap = rx_pkts - tx_pkts;

        if (rx_tx_gap > RX_TX_PKTS_GAP)
        {
            ERROR("Rx CSAP captured more packets than were requested (%u > %u, gap %u > %u)",
                  rx_pkts, tx_pkts, rx_tx_gap, RX_TX_PKTS_GAP);
            ERROR_VERDICT("Rx CSAP captured more packets than were sent");
        }

        rx_pkts = tx_pkts;
    }

    get_timestamps(sender_rpcs->ta, csap_send, &tx_ts);
    tx_time_s = tx_ts.dur.tv_sec + (double)tx_ts.dur.tv_usec / TE_SEC2US(1);
    if (rx_pkts > 0)
    {
        get_timestamps(receiver_rpcs->ta, csap_rx, &rx_ts);
        rx_time_s = rx_ts.dur.tv_sec +
                    (double)rx_ts.dur.tv_usec / TE_SEC2US(1);
    }

    if (tx_time_s > 0.0)
    {
        tx_pps = tx_pkts / tx_time_s;
        tx_l1_bps = tx_pps * pkt_l1_bits;
    }
    if (rx_time_s > 0.0)
    {
        rx_pps = rx_pkts / rx_time_s;
        rx_l1_bps = rx_pps * pkt_l1_bits;
    }

    loss_pkts = (int64_t)tx_pkts - rx_pkts;
    if (tx_pkts > 0)
        loss_pct = (double)loss_pkts * 100.0 / tx_pkts;

    min_l1_bps = (double)link_speed_bps * MIN_SPEED_MULTIPLIER;
    checked_dir = iut_is_sender ? "IUT->Tester" : "Tester->IUT";
    checked_l1_bps = iut_is_sender ? rx_l1_bps : tx_l1_bps;
    checked_iface = iut_is_sender ? sender_iface->if_name :
                                    receiver_iface->if_name;

    TEST_ARTIFACT("Frame size: %d bytes", frame_size);
    TEST_ARTIFACT("Ethernet payload length: %d bytes", payload_len);
    TEST_ARTIFACT("Link speed: %d Mbps", link_speed_mbps);
    TEST_ARTIFACT("Minimum acceptable throughput: %.2f Mbps",
                  TE_UNITS_DEC_U2M(min_l1_bps));
    TEST_ARTIFACT("Target load duration: %d s", load_duration);
    TEST_ARTIFACT("Target packet rate at line rate: %.3f pps", target_pps);
    TEST_ARTIFACT("Calculated packets to send: %" PRIu64, target_packets);
    TEST_ARTIFACT("Tx packets: %u", tx_pkts);
    TEST_ARTIFACT("Rx packets: %u", rx_pkts);
    TEST_ARTIFACT("Frame loss: %" TE_PRINTF_64 "d packets (%.3f%%)",
                  loss_pkts, loss_pct);

    add_pps_artifacts(tx_pps, rx_pps);
    add_l1_bitrate_artifacts(tx_l1_bps, rx_l1_bps);

    log_summary_mi(tx_pps, rx_pps, tx_l1_bps, rx_l1_bps);

    if (loss_pct > max_loss_pct)
    {
        ERROR("Frame loss %.3f%% exceeds max_loss_pct %.3f%%",
              loss_pct, max_loss_pct);
        ERROR_VERDICT("Frame loss exceeds configured threshold");
    }

    if (checked_l1_bps < EPS)
    {
        ERROR("Zero throughput in %s direction (IUT interface: %s)",
              checked_dir, checked_iface);
        TEST_VERDICT("Zero IUT throughput in checked direction");
    }
    else if (checked_l1_bps < min_l1_bps)
    {
        ERROR("Throughput is too low in %s direction "
              "(IUT interface: %s, L1 %.2f Mbps < %.2f Mbps threshold)",
              checked_dir, checked_iface, TE_UNITS_DEC_U2M(checked_l1_bps),
              TE_UNITS_DEC_U2M(min_l1_bps));
        TEST_VERDICT("IUT throughput is too low in checked direction");
    }

    TEST_SUCCESS;

cleanup:
    CLEANUP_CHECK_RC(tapi_tad_csap_destroy_all(0));

    asn_free_value(tmpl);

    CLEANUP_CHECK_RC(tapi_env_stats_gather_and_log_diff(&env));

    TEST_END;
}

/** @} */
