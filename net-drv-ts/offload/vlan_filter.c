/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Offload tests
 */

/**
 * @defgroup offload-vlan_filter Check VLAN filter offload
 * @ingroup offload
 * @{
 *
 * @objective Check VLAN filter offload functionality.
 *
 * @param env               Testing environment:
 *                          - @ref env-peer2peer
 *                          - @ref env-peer2peer_ipv6
 * @param vlan_filter_on    If @c true, enable VLAN filter offload on
 *                          IUT interface, otherwise disable it.
 *
 * @par Scenario:
 *
 * @author Andrew Rybchenko <andrew.rybchenko@oktetlabs.ru>
 */

#define TE_TEST_NAME "offload/vlan_filter"

#include "net_drv_test.h"
#include "te_ethernet.h"
#include "tapi_udp.h"
#include "tapi_cfg.h"
#include "ndn.h"
#include "tapi_cfg_base.h"
#include "tapi_ndn.h"

#define MAX_PKT_LEN 1024

static rcf_rpc_server *iut_rpcs = NULL;
static rcf_rpc_server *tst_rpcs = NULL;
static bool vlan_filter_on;
static csap_handle_t csap_tst = CSAP_INVALID_HANDLE;
static csap_handle_t csap_iut = CSAP_INVALID_HANDLE;

/**
 * Send UDP packet from Tester to IUT with specified VLAN TCI.
 */
static void
send_recv_pkt(int af, uint16_t vlan_tci, bool exp_receive, const char *stage)
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

    CHECK_RC(tapi_ndn_pkt_inject_vlan_tag(pkt_templ, vlan_tci));

    send_len = rand_range(1, sizeof(send_buf));
    te_fill_buf(send_buf, send_len);

    CHECK_RC(asn_write_value_field(pkt_templ, send_buf, send_len,
                                   "payload.#bytes"));

    CHECK_RC(tapi_tad_trsend_start(tst_rpcs->ta, 0, csap_tst,
                                   pkt_templ, RCF_MODE_BLOCKING));

    CHECK_RC(tapi_tad_trrecv_get(iut_rpcs->ta, 0, csap_iut, NULL,
                                 &rx_pkts_num));
    if (rx_pkts_num > 1)
    {
        TEST_VERDICT("%s: CSAP on IUT captured more than one packet",
                     stage);
    }

    if (exp_receive)
    {
        if (rx_pkts_num == 0)
        {
            TEST_VERDICT("%s: CSAP on IUT did not capture the packet",
                         stage);
        }
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

    uint16_t vlan_a_id;
    char *vlan_a_if = NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_LINK_ADDR(iut_lladdr);
    TEST_GET_BOOL_PARAM(vlan_filter_on);

    TEST_STEP("Set @b rx-vlan-filter feature on the IUT interface according "
              "to value of @p vlan_filter_on test parameter.");
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "rx-vlan-filter", vlan_filter_on ? 1 : 0);

    TEST_STEP("Ensure that promiscuous mode is disabled on the IUT interface.");
    CHECK_RC(tapi_cfg_base_if_set_promisc(iut_rpcs->ta, iut_if->if_name,
                                          false));

    CFG_WAIT_CHANGES;

    TEST_STEP("Allocate a pair of IP addresses to be used in VLAN.");
    CHECK_RC(tapi_cfg_alloc_af_net_addr_pair(iut_addr->sa_family,
                                             &iut_addr2, &tst_addr2, NULL));

    TEST_STEP("Create a CSAP on IUT to capture UDP packets sent "
              "to @p iut_addr2.");

    CHECK_RC(tapi_udp_ip_eth_csap_create(iut_rpcs->ta, 0, iut_if->if_name,
                                         TAD_ETH_RECV_DEF |
                                         TAD_ETH_RECV_NO_PROMISC,
                                         (uint8_t *)(iut_lladdr->sa_data),
                                         (uint8_t *)(tst_lladdr->sa_data),
                                         iut_addr2->sa_family,
                                         TAD_SA2ARGS(iut_addr2, tst_addr2),
                                         &csap_iut));

    CHECK_RC(tapi_tad_trrecv_start(iut_rpcs->ta, 0, csap_iut, NULL,
                                   TAD_TIMEOUT_INF, 0, RCF_TRRECV_COUNT));

    TEST_STEP("Create a CSAP on Tester to send packets to @p iut_addr2.");
    CHECK_RC(tapi_udp_ip_eth_csap_create(tst_rpcs->ta, 0, tst_if->if_name,
                                         TAD_ETH_RECV_NO,
                                         (uint8_t *)(tst_lladdr->sa_data),
                                         (uint8_t *)(iut_lladdr->sa_data),
                                         iut_addr2->sa_family,
                                         TAD_SA2ARGS(tst_addr2, iut_addr2),
                                         &csap_tst));

    TEST_STEP("Send a packet to @p vlan_a_id when the VLAN is not added yet.");
    vlan_a_id = rand_range(1, 4094);
    send_recv_pkt(iut_addr2->sa_family, vlan_a_id, !vlan_filter_on,
                  "Sending to the not added VLAN");

    TEST_STEP("Add the VLAN @p vlan_a_id to the @p iut_if.");
    CHECK_RC(tapi_cfg_base_if_add_vlan(iut_rpcs->ta, iut_if->if_name,
                                       vlan_a_id, &vlan_a_if));
    CFG_WAIT_CHANGES;

    TEST_STEP("Send a packet to @p vlan_a_id when the VLAN is added.");
    send_recv_pkt(iut_addr2->sa_family, vlan_a_id, true,
                  "Sending to the just added VLAN");

    TEST_STEP("Delete the VLAN @p vlan_a_id from the @p iut_if.");
    CHECK_RC(tapi_cfg_base_if_del_vlan(iut_rpcs->ta, iut_if->if_name,
                                       vlan_a_id));
    CFG_WAIT_CHANGES;

    TEST_STEP("Send a packet to @p vlan_a_id when the VLAN is removed.");
    send_recv_pkt(iut_addr2->sa_family, vlan_a_id, !vlan_filter_on,
                  "Sending to the removed VLAN");

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0, csap_iut));
    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0, csap_tst));

    TEST_END;
}
