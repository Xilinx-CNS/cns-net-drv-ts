/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2025 OKTET Labs Ltd. All rights reserved. */
/**
 * @defgroup fwd_prologue Fowarding tests prologue
 * @ingroup net_drv_tests
 * @{
 *
 * @objective Convert two parallel links configuration to non-parallel
 *
 * @param env               Testing environment:
 *                          - @ref env-peer2peerX2
 *
 * @par Scenario:
 */

/** Logging subsystem entity name */
#define TE_TEST_NAME    "fwd_prologue"

#include "net_drv_test.h"

#include "tapi_cfg.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_net.h"
#include "tapi_env.h"

int
main(int argc, char **argv)
{
    const struct if_nameindex *iut_if0 = NULL;
    const struct if_nameindex *iut_if1 = NULL;
    const cfg_net_node_t *tst_if1_node = NULL;
    const struct sockaddr *iut4_addr0 = NULL;
    const struct sockaddr *iut6_addr0 = NULL;
    const struct sockaddr *tst4_addr0 = NULL;
    const struct sockaddr *tst6_addr0 = NULL;
    const struct sockaddr *iut4_addr1 = NULL;
    const struct sockaddr *iut6_addr1 = NULL;
    const struct sockaddr *tst4_addr1 = NULL;
    const struct sockaddr *tst6_addr1 = NULL;
    const tapi_env_if *tst_if1 = NULL;
    tapi_env_host *iut_host = NULL;
    tapi_env_host *tst_host = NULL;
    char *tst_ta_ns1 = NULL;

    TEST_START;
    TEST_GET_IF(iut_if0);
    TEST_GET_IF(iut_if1);
    TEST_GET_ENV_IF(tst_if1);
    TEST_GET_HOST(iut_host);
    TEST_GET_HOST(tst_host);
    TEST_GET_ADDR_NO_PORT(iut4_addr0);
    TEST_GET_ADDR_NO_PORT(iut6_addr0);
    TEST_GET_ADDR_NO_PORT(tst4_addr0);
    TEST_GET_ADDR_NO_PORT(tst6_addr0);
    TEST_GET_ADDR_NO_PORT(iut4_addr1);
    TEST_GET_ADDR_NO_PORT(iut6_addr1);
    TEST_GET_ADDR_NO_PORT(tst4_addr1);
    TEST_GET_ADDR_NO_PORT(tst6_addr1);

    CHECK_NOT_NULL(tst_if1_node = tapi_env_get_if_net_node(tst_if1));

    TEST_STEP("Move the second tester interface to separate network namespace pass traffic between tester interfaces via IUT.");
    CHECK_RC(tapi_cfg_net_node_to_netns(tst_if1_node, "ns1", &tst_ta_ns1));

    TEST_STEP("Enable IPv4 forwarding on IUT interfaces.");
    CHECK_RC(tapi_cfg_base_ipv4_fw(iut_host->ta, true));
    CHECK_RC(tapi_cfg_ipv4_fw_enable(iut_host->ta, iut_if0->if_name));
    CHECK_RC(tapi_cfg_ipv4_fw_enable(iut_host->ta, iut_if1->if_name));

    TEST_STEP("Enable IPv6 forwarding on IUT interfaces.");
    CHECK_RC(cfg_set_instance_fmt(CFG_VAL(BOOL, true), "/agent:%s/ip6_fw:",
                                  iut_host->ta));
    CHECK_RC(tapi_cfg_ipv6_fw_enable(iut_host->ta, iut_if0->if_name));
    CHECK_RC(tapi_cfg_ipv6_fw_enable(iut_host->ta, iut_if1->if_name));

    TEST_STEP("Add IPv4 routes on TST to route traffic via IUT.");
    CHECK_RC(tapi_cfg_add_route_simple(tst_host->ta, tst4_addr1, 32,
                                       iut4_addr0, NULL));
    CHECK_RC(tapi_cfg_add_route_simple(tst_ta_ns1, tst4_addr0, 32,
                                       iut4_addr1, NULL));

    TEST_STEP("Add IPv6 routes on TST to route traffic via IUT.");
    CHECK_RC(tapi_cfg_add_route_simple(tst_host->ta, tst6_addr1, 128,
                                       iut6_addr0, NULL));
    CHECK_RC(tapi_cfg_add_route_simple(tst_ta_ns1, tst6_addr0, 128,
                                       iut6_addr1, NULL));

    TEST_STEP("Dump configuration to logs.");
    CHECK_RC(rc = cfg_synchronize("/:", TRUE));
    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    TEST_SUCCESS;

cleanup:
    free(tst_ta_ns1);

    TEST_END;
}
