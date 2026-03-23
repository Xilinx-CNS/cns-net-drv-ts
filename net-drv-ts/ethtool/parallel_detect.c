/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-parallel_detect Check parallel detect with forced partner.
 * @ingroup ethtool
 * @{
 *
 * @objective Check that an autonegotiating endpoint detects link speed from
 *            a forced link partner.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *
 * @author Denis Pryazhennikov <denis.pryazhennikov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "ethtool/parallel_detect"

#include "net_drv_test.h"
#include "net_drv_phy.h"

typedef struct forced_mode_info {
    const char *full_mode_name;
    const char *half_mode_name;
    int speed;
    int duplex;
} forced_mode_info;

static void
check_tp_port_or_skip(const char *who, const char *ta, const char *if_name)
{
    enum te_phy_port port;

    CHECK_RC(tapi_cfg_phy_port_get(ta, if_name, &port));
    if (port != TE_PHY_PORT_TP)
    {
        WARN("%s PHY port is %s",  who, tapi_cfg_phy_port_id2str(port));
        TEST_SKIP("Unsupported PHY for parallel detecting");
    }
}

static const forced_mode_info *
find_common_forced_mode(const net_drv_phy_saved_state *iut_state,
                        const net_drv_phy_saved_state *tst_state)
{
    static const forced_mode_info candidates[] = {
        { "100baseT_Full", "100baseT_Half", 100, TE_PHY_DUPLEX_FULL },
        { "10baseT_Full", "10baseT_Half", 10, TE_PHY_DUPLEX_FULL },
    };
    unsigned int i;

    for (i = 0; i < TE_ARRAY_LEN(candidates); i++)
    {
        if (net_drv_phy_state_find_mode_by_name(
                iut_state, candidates[i].half_mode_name) != NULL &&
            net_drv_phy_state_find_mode_by_name(
                iut_state, candidates[i].full_mode_name) != NULL &&
            net_drv_phy_state_find_mode_by_name(
                tst_state, candidates[i].full_mode_name) != NULL)
        {
            return &candidates[i];
        }
    }

    return NULL;
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

    net_drv_phy_saved_state iut_saved = {};
    net_drv_phy_saved_state tst_saved = {};
    const forced_mode_info *forced_mode = NULL;
    int iut_s = -1;
    int tst_s = -1;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);

    TEST_STEP("Save current PHY configuration on both endpoints.");
    net_drv_phy_state_save_or_skip("IUT", iut_rpcs->ta, iut_if->if_name,
                                   &iut_saved);
    net_drv_phy_state_save_or_skip("Tester", tst_rpcs->ta,
                                   tst_if->if_name, &tst_saved);

    TEST_STEP("Check that both PHY ports are twisted pair.");
    check_tp_port_or_skip("IUT", iut_rpcs->ta, iut_if->if_name);
    check_tp_port_or_skip("Tester", tst_rpcs->ta, tst_if->if_name);

    TEST_STEP("Choose a common exact PHY mode suitable for parallel detect.");
    forced_mode = find_common_forced_mode(&iut_saved, &tst_saved);
    if (forced_mode == NULL)
        TEST_SKIP("Parallel detect requires common 10/100 PHY modes");

    RING("Selected forced PHY mode: %s (%d/%s)",
         forced_mode->full_mode_name, forced_mode->speed,
         tapi_cfg_phy_duplex_id2str(forced_mode->duplex));

    TEST_STEP("Establish a connection between IUT and Tester to check data flow.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Advertise the selected full- and half-duplex modes.");
    CHECK_RC(net_drv_phy_apply_autoneg_modes(iut_rpcs->ta, iut_if->if_name,
                                             &iut_saved,
                                             forced_mode->full_mode_name,
                                             forced_mode->half_mode_name));

    TEST_STEP("Force the selected speed and full duplex on Tester.");
    CHECK_RC(net_drv_phy_apply_fixed_mode(tst_rpcs->ta, tst_if->if_name,
                                          forced_mode->speed,
                                          forced_mode->duplex));
    NET_DRV_WAIT_PHY_CHANGE;

    TEST_STEP("Wait until link comes up on both sides.");
    net_drv_wait_up(iut_rpcs->ta, iut_if->if_name);
    net_drv_wait_up(tst_rpcs->ta, tst_if->if_name);

    TEST_STEP("Check local autonegotiation state on both endpoints.");
    net_drv_phy_check_autoneg_state("IUT", iut_rpcs->ta, iut_if->if_name,
                                    TE_PHY_AUTONEG_ON);
    net_drv_phy_check_autoneg_state("Tester", tst_rpcs->ta, tst_if->if_name,
                                    TE_PHY_AUTONEG_OFF);

    TEST_STEP("Check that IUT sees link partner autonegotiation disabled.");
    net_drv_phy_check_lp_autoneg_state("IUT", iut_rpcs->ta, iut_if->if_name,
                                       TE_PHY_AUTONEG_OFF);

    TEST_STEP("Check negotiated operational mode after parallel detect.");
    net_drv_phy_check_oper_mode("IUT", iut_rpcs->ta, iut_if->if_name,
                                forced_mode->speed, TE_PHY_DUPLEX_HALF);
    net_drv_phy_check_oper_mode("Tester", tst_rpcs->ta, tst_if->if_name,
                                forced_mode->speed, forced_mode->duplex);

    TEST_STEP("Check that traffic flows after parallel detect.");
    net_drv_conn_check(iut_rpcs, iut_s, "IUT socket",
                       tst_rpcs, tst_s, "Tester socket",
                       "Checking data flow after parallel detect");

    TEST_SUCCESS;

cleanup:

    if (iut_saved.saved)
    {
        CLEANUP_CHECK_RC(net_drv_phy_state_restore(iut_rpcs->ta,
                                                   iut_if->if_name,
                                                   &iut_saved));
    }
    if (tst_saved.saved)
    {
        CLEANUP_CHECK_RC(net_drv_phy_state_restore(tst_rpcs->ta,
                                                   tst_if->if_name,
                                                   &tst_saved));
    }
    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);
    if (iut_saved.saved || tst_saved.saved)
    {
        net_drv_wait_up_gen(iut_rpcs->ta, iut_if->if_name, true);
        net_drv_wait_up_gen(tst_rpcs->ta, tst_if->if_name, true);
    }

    net_drv_phy_state_free(&iut_saved);
    net_drv_phy_state_free(&tst_saved);

    TEST_END;
}

/** @} */
