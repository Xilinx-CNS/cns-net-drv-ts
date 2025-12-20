/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024-2025 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-loopback Check loopback mode
 * @ingroup ethtool
 * @{
 *
 * @objective Check that loopback mode works correctly.
 *
 * @param env           Testing environment:
 *                       - @ref env-peer2peer
 * @param tmpl          Traffic template
 *
 * @par Scenario:
 *
 * @author Ilia Ismailov <ilia.ismailov@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/loopback"

#include "net_drv_test.h"
#include "te_ethernet.h"
#include "tapi_ndn.h"
#include "tapi_eth.h"
#include "tapi_cfg_if.h"

#define TEST_RECV_WAIT_MS 500
#define TEST_SEND_DELAY_MS 1000

int
main(int argc, char *argv[])
{
    rcf_rpc_server             *iut_rpcs = NULL;
    rcf_rpc_server             *tst_rpcs = NULL;
    const struct if_nameindex  *iut_if = NULL;
    const struct if_nameindex  *tst_if = NULL;
    asn_value                  *tmpl = NULL;

    unsigned int                payload_len;
    uint8_t                     payload[ETHER_DATA_LEN];
    asn_value                  *ptrn = NULL;
    csap_handle_t               iut_rx = CSAP_INVALID_HANDLE;
    csap_handle_t               tst_rx = CSAP_INVALID_HANDLE;
    unsigned int                tst_received;
    unsigned int                tst_no_match_pkts;
    unsigned int                iut_received;
    unsigned int                iut_no_match_pkts;
    bool                        fail = false;
    bool                        disable_source_pruning;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);

    /*
     * On i40e it looks important to disable source pruning before
     * loopback enable.
     */
    TEST_STEP("If supported, disable source pruning on @p iut_if to be able to get packets with its own MAC address");
    rc = tapi_cfg_if_priv_flag_get(iut_rpcs->ta, iut_if->if_name,
            "disable-source-pruning", &disable_source_pruning);
    switch (TE_RC_GET_ERROR(rc))
    {
        case 0:
            if (!disable_source_pruning)
                CHECK_RC(tapi_cfg_if_priv_flag_set(iut_rpcs->ta,
                                                   iut_if->if_name,
                                                   "disable-source-pruning",
                                                   true));
            break;

        case TE_ENOENT:
            /* Nothing to do */
            break;

        default:
            TEST_FAIL("Failed to get disable-source-pruning on IUT interface %s:: $r",
                      iut_if->if_name, rc);
    }

    TEST_STEP("Enable loopback on @p iut_if");
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name, "loopback", 1);

    TEST_STEP("Prepare traffic template to be transmitted");
    CHECK_RC(tapi_ndn_subst_env(tmpl, NULL, &env));
    payload_len = rand_range(0, sizeof(payload));
    te_fill_buf(payload, payload_len);
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tmpl, false, payload,
                                                  payload_len));

    CHECK_RC(tapi_ndn_tmpl_to_ptrn(tmpl, &ptrn));

    TEST_STEP("Ensure that @p iut_if is up");
    net_drv_wait_up(iut_rpcs->ta, iut_if->if_name);

    TEST_STEP("Create CSAP and start to receive packets on @p iut_if");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(iut_rpcs->ta, 0,
                                                iut_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tmpl, &iut_rx));
    CHECK_RC(tapi_tad_trrecv_start(iut_rpcs->ta, 0, iut_rx, ptrn,
                                   TEST_SEND_DELAY_MS + TEST_RECV_WAIT_MS, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    TEST_STEP("Create CSAP and start to receive packets on @p tst_if");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_rpcs->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tmpl, &tst_rx));
    CHECK_RC(tapi_tad_trrecv_start(tst_rpcs->ta, 0, tst_rx, ptrn,
                                   TEST_SEND_DELAY_MS + TEST_RECV_WAIT_MS, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_MISMATCH));

    /*
     * i40e requires to bring link up on Tester side as well, but it is
     * not always true. So, just give some time to bring link up.
     */
    MSLEEP(TEST_SEND_DELAY_MS);

    TEST_STEP("Transmit packet by template via @p iut_if");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(iut_rpcs->ta, 0,
                                                iut_if->if_name, tmpl,
                                                NULL, NULL));

    TEST_STEP("Check that no packets are received on @p tst_if");
    rc = tapi_tad_trrecv_wait(tst_rpcs->ta, 0, tst_rx, NULL, &tst_received);
    if (rc != 0 && TE_RC_GET_ERROR(rc) != TE_ETIMEDOUT)
        TEST_FAIL("Failed to wait for traffic receive on Tester: %r", rc);
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(tst_rpcs->ta, 0, tst_rx,
                                             &tst_no_match_pkts));
    if (tst_received != 0)
    {
        ERROR_VERDICT("Sent packet is received on Tester regardless loopback");
        fail = true;
    }
    if (tst_no_match_pkts != 0)
    {
        WARN("Unexpected packets received on Tester");
    }

    TEST_STEP("Check that sent packet is received on @p iut_if via loopback");
    rc = tapi_tad_trrecv_wait(iut_rpcs->ta, 0, iut_rx, NULL, &iut_received);
    if (rc != 0 && TE_RC_GET_ERROR(rc) != TE_ETIMEDOUT)
        TEST_FAIL("Failed to wait for traffic receive on IUT: %r", rc);
    CHECK_RC(tapi_tad_csap_get_no_match_pkts(iut_rpcs->ta, 0, iut_rx,
                                             &iut_no_match_pkts));
    if (iut_received == 0)
    {
        ERROR_VERDICT("Sent packet is not received on IUT with loopback");
        fail = true;
    }
    if (iut_received > 1)
    {
        ERROR_VERDICT("Duplicate sent packet are received on IUT with loopback");
        fail = true;
    }
    if (iut_no_match_pkts != 0)
    {
        WARN("Unexpected packets received on IUT");
    }

    if (fail)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0, iut_rx));
    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0, tst_rx));
    asn_free_value(ptrn);
    asn_free_value(tmpl);

    TEST_END;
}
