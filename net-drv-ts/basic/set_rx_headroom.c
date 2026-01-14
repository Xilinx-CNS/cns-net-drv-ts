/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2024 OKTET LABS Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-set_rx_headroom Check that the driver still works fine after
 *                                 headroom changes.
 * @ingroup basic
 * @{
 *
 * @objective Check that the driver still works fine after headroom changes
 *            triggered by usage together with VxLAN tunnel in the bridge.
 *
 * @param env           Testing environment.
 *                      - @ref env-peer2peer
 *                      - @ref env-peer2peer_ipv6
 *
 * @par Scenario:
 *
 * @author Daniil Byshenko <daniil.byshenko@oktetlabs.ru>
 */

#define TE_TEST_NAME "basic/set_rx_headroom"

#include "net_drv_test.h"
#include "te_ethernet.h"
#include "tapi_cfg.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_if.h"
#include "tapi_cfg_bridge.h"
#include "tapi_cfg_tunnel.h"
#include "tapi_cfg_tap.h"
#include "tapi_host_ns.h"
#include "tapi_udp.h"
#include "tapi_ndn.h"
#include "tapi_serial_parse.h"
#include "ndn.h"
#include "tad_common.h"
#include "te_rand.h"

#define TAP_NAME            "tap0"
#define BRIDGE_NAME         "bridge0"

#define VXLAN_VNI_MAX       0xFFFFFF
#define VXLAN_TNL_NAME      "vxlan0"
#define VXLAN_PORT          4789

#define MAX_PKT_LEN 1024

#define PKTS_NUM_WAIT 1

/* Maximum possible log level */
#define CONSOLE_LOGLEVEL 8

static rcf_rpc_server *iut_rpcs = NULL;
static rcf_rpc_server *tst_rpcs = NULL;
static csap_handle_t csap_tst = CSAP_INVALID_HANDLE;
static csap_handle_t csap_iut = CSAP_INVALID_HANDLE;

/**
 * Send UDP packet from Tester to IUT.
 */
static void
send_recv_pkt(int af, bool exp_receive, const char *stage)
{
    char buf_tmpl[1024];
    asn_value *pkt_templ = NULL;
    char send_buf[MAX_PKT_LEN];
    int send_len;
    int num;

    unsigned int rx_pkts_num;

    CHECK_RC(te_snprintf(buf_tmpl, sizeof(buf_tmpl),
                         "{ pdus { udp: {}, ip%d:{}, eth:{} } }",
                         (af == AF_INET ? 4 : 6)));

    CHECK_RC(asn_parse_value_text(buf_tmpl, ndn_traffic_template,
                                  &pkt_templ, &num));

    send_len = rand_range(1, sizeof(send_buf));
    te_fill_buf(send_buf, send_len);

    CHECK_RC(asn_write_value_field(pkt_templ, send_buf, send_len,
                                   "payload.#bytes"));

    CHECK_RC(tapi_tad_trsend_start(tst_rpcs->ta, 0, csap_tst,
                                   pkt_templ, RCF_MODE_BLOCKING));

    CHECK_RC(tapi_tad_trrecv_wait_pkts_get_num(iut_rpcs->ta, 0, csap_iut,
                                               PKTS_NUM_WAIT,
                                               TAPI_WAIT_NETWORK_DELAY,
                                               &rx_pkts_num));
    if (rx_pkts_num > PKTS_NUM_WAIT)
        TEST_VERDICT("%s: CSAP on IUT captured more than one packet", stage);

    if (exp_receive)
    {
        if (rx_pkts_num == 0)
            TEST_VERDICT("%s: CSAP on IUT did not capture the packet", stage);
    }
    else
    {
        if (rx_pkts_num != 0)
            TEST_VERDICT("%s: CSAP on IUT captured the packet", stage);
    }

    free(pkt_templ);
}

int
main(int argc, char *argv[])
{
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;

    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    struct sockaddr *iut_addr2 = NULL;
    struct sockaddr *tst_addr2 = NULL;

    const struct sockaddr *tst_lladdr = NULL;
    const struct sockaddr *iut_lladdr = NULL;

    int logs_nb = 0;
    tapi_parser_id id;

    te_bool if_down_up;

    tapi_cfg_bridge bridge_config;
    tapi_cfg_tunnel tunnel_config;
    int32_t tunnel_vni = rand_range(1, VXLAN_VNI_MAX);

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_LINK_ADDR(iut_lladdr);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_BOOL_PARAM(if_down_up);

    memset(&id, 0, sizeof(id));
    id.ta = "LogListener";
    id.name = "iut";

    TEST_STEP("Configure serial parser to check for driver logs on IUT.");
    CHECK_RC(tapi_cfg_set_loglevel(iut_rpcs->ta, CONSOLE_LOGLEVEL));
    CHECK_RC(tapi_serial_parser_event_add(&id, "headroom_log", ""));

    rc = tapi_serial_parser_pattern_add(&id, "headroom_log", "Rx headroom");
    if (rc < 0)
        TEST_FAIL("Failed to add a parser pattern");

    TEST_STEP("Enable all flags in @b msglvl for IUT interface.");
    rc = tapi_cfg_if_msglvl_set(iut_rpcs->ta, iut_if->if_name,
                                TAPI_NETIF_MSG_ALL);
    if (rc != 0)
        RING_VERDICT("Failed to enable all flags in msglvl");

    TEST_STEP("Create TAP interface on IUT.");
    CHECK_RC(tapi_cfg_tap_add(iut_rpcs->ta, TAP_NAME));
    CHECK_RC(tapi_cfg_base_if_up(iut_rpcs->ta, TAP_NAME));

    CFG_WAIT_CHANGES;

    TEST_STEP("Create VxLAN interface on IUT.");

    TEST_SUBSTEP("Allocate a pair of IP addresses for VxLAN tunnel.");
    CHECK_RC(tapi_cfg_alloc_af_net_addr_pair(iut_addr->sa_family,
                                             &iut_addr2, &tst_addr2, NULL));

    tunnel_config = (tapi_cfg_tunnel) {
        .type = TAPI_CFG_TUNNEL_TYPE_VXLAN,
        .tunnel_name = VXLAN_TNL_NAME,
        .vxlan = (tapi_cfg_tunnel_vxlan) {
            .if_name = TAP_NAME,
            .local = SA(iut_addr2),
            .remote = SA(tst_addr2),
            .vni = tunnel_vni,
            .port = VXLAN_PORT,
        },
    };

    TEST_SUBSTEP("Create new VxLAN tunnel interface on IUT.");
    CHECK_RC(tapi_cfg_tunnel_add(iut_rpcs->ta, &tunnel_config));

    TEST_STEP("Start new VxLAN interface on IUT.");
    CHECK_RC(tapi_cfg_tunnel_enable(iut_rpcs->ta, &tunnel_config));

    CFG_WAIT_CHANGES;

    TEST_STEP("Create new bridge with 2 interfaces on IUT.");

    bridge_config = (tapi_cfg_bridge) {
        .bridge_name = BRIDGE_NAME,
    };

    TEST_SUBSTEP("Create new bridge on IUT.");
    CHECK_RC(tapi_cfg_bridge_add(iut_rpcs->ta, &bridge_config));

    TEST_SUBSTEP("Up new bridge interface on IUT.");
    CHECK_RC(tapi_cfg_base_if_up(iut_rpcs->ta, BRIDGE_NAME));

    TEST_SUBSTEP("Add IUT interface to the bridge.");
    CHECK_RC(tapi_cfg_bridge_port_add(iut_rpcs->ta, BRIDGE_NAME,
                                      iut_if->if_name));

    TEST_SUBSTEP("Add VxLAN interface to the bridge to trigger Rx headroom "
                 "changes on already added IUT interface.");
    CHECK_RC(tapi_cfg_bridge_port_add(iut_rpcs->ta, BRIDGE_NAME,
                                      VXLAN_TNL_NAME));

    CFG_WAIT_CHANGES;

    if (if_down_up)
    {
        TEST_STEP("If @p if_down_up is @c TRUE, set the IUT interface "
                  "DOWN and UP.");
        CHECK_RC(tapi_cfg_base_if_down_up(iut_rpcs->ta, iut_if->if_name));
    }

    TEST_STEP("Create a CSAP on IUT to capture packets sent to @p iut_addr.");
    CHECK_RC(tapi_udp_ip_eth_csap_create(iut_rpcs->ta, 0, iut_if->if_name,
                                         TAD_ETH_RECV_DEF |
                                         TAD_ETH_RECV_NO_PROMISC,
                                         (uint8_t *)(iut_lladdr->sa_data),
                                         (uint8_t *)(tst_lladdr->sa_data),
                                         iut_addr->sa_family,
                                         TAD_SA2ARGS(iut_addr, tst_addr),
                                         &csap_iut));

    CHECK_RC(tapi_tad_trrecv_start(iut_rpcs->ta, 0, csap_iut, NULL,
                                   TAD_TIMEOUT_INF, 0, RCF_TRRECV_COUNT));

    TEST_STEP("Create a CSAP on Tester to send packets to @p iut_addr.");
    CHECK_RC(tapi_udp_ip_eth_csap_create(tst_rpcs->ta, 0, tst_if->if_name,
                                         TAD_ETH_RECV_NO,
                                         (uint8_t *)(tst_lladdr->sa_data),
                                         (uint8_t *)(iut_lladdr->sa_data),
                                         iut_addr->sa_family,
                                         TAD_SA2ARGS(tst_addr, iut_addr),
                                         &csap_tst));

    TEST_STEP("Send a packet from Tester and check that it is received "
              "successfully on IUT interface.");
    send_recv_pkt(iut_addr->sa_family, true, "Sending packet");

    TEST_STEP("Check that driver printed some logs about headroom.");
    CHECK_RC(tapi_serial_parser_event_get_count(&id, "headroom_log", &logs_nb));

    RING("Detected %d driver headroom logs when logging was enabled", logs_nb);

    if (logs_nb > 0)
        RING_VERDICT("Headroom changing logs from the driver were detected");

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0, csap_iut));
    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0, csap_tst));

    TEST_END;
}

/** @} */
