/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-link_fixed_mode Check fixed PHY mode switching
 * @ingroup ethtool
 * @{
 *
 * @objective Check that traffic passes in all common forceable fixed PHY
 *            modes.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *
 * @author Denis Pryazhennikov <denis.pryazhennikov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "ethtool/link_fixed_mode"

#include <string.h>

#include "net_drv_test.h"
#include "net_drv_phy.h"
#include "te_string.h"

static bool
is_mode_pair_selected(const net_drv_phy_mode_info **modes,
                      unsigned int modes_num,
                      const net_drv_phy_mode_info *mode)
{
    unsigned int i;

    for (i = 0; i < modes_num; i++)
    {
        if (modes[i]->speed == mode->speed && modes[i]->duplex == mode->duplex)
            return true;
    }

    return false;
}

static te_errno
find_common_mode_candidates(const net_drv_phy_saved_state *iut_state,
                            const net_drv_phy_saved_state *tst_state,
                            const net_drv_phy_mode_info ***candidates,
                            unsigned int *candidates_num)
{
    const net_drv_phy_mode_info **found_modes;
    const net_drv_phy_mode_info *peer_mode;
    unsigned int found_num = 0;
    unsigned int i;

    *candidates = NULL;
    *candidates_num = 0;

    if (iut_state->modes_num == 0)
        return 0;

    found_modes = TE_ALLOC(iut_state->modes_num * sizeof(*found_modes));

    for (i = 0; i < iut_state->modes_num; i++)
    {
        if (!net_drv_phy_is_link_mode(&iut_state->modes[i]))
            continue;

        peer_mode = net_drv_phy_state_find_mode_by_name(
                        tst_state, iut_state->modes[i].name);
        if (peer_mode == NULL || !net_drv_phy_is_link_mode(peer_mode))
            continue;

        if (is_mode_pair_selected(found_modes, found_num,
                                  &iut_state->modes[i]))
        {
            continue;
        }

        found_modes[found_num++] = &iut_state->modes[i];
    }

    if (found_num == 0)
    {
        free(found_modes);
        return 0;
    }

    *candidates = found_modes;
    *candidates_num = found_num;

    return 0;
}

int
main(int argc, char *argv[])
{
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;

    const net_drv_phy_mode_info **mode_candidates = NULL;
    const net_drv_phy_mode_info *mode = NULL;
    net_drv_phy_saved_state iut_saved = {};
    net_drv_phy_saved_state tst_saved = {};
    unsigned int candidates_num = 0;
    unsigned int tested_num = 0;
    int iut_s = -1;
    int tst_s = -1;
    unsigned int i;

    te_string traffic_step = TE_STRING_INIT;
    te_string iut_where = TE_STRING_INIT;
    te_string tst_where = TE_STRING_INIT;

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
    net_drv_phy_state_save_or_skip("Tester", tst_rpcs->ta, tst_if->if_name,
                                   &tst_saved);

    TEST_STEP("Find common supported PHY modes exposed on both endpoints.");
    CHECK_RC(find_common_mode_candidates(&iut_saved, &tst_saved,
                                         &mode_candidates,
                                         &candidates_num));
    if (candidates_num == 0)
        TEST_SKIP("No common supported PHY modes found");

    RING("Found %u common supported PHY mode candidate(s)", candidates_num);
    for (i = 0; i < candidates_num; i++)
    {
        RING("Common mode %u: (%d/%s)", i + 1,
             mode_candidates[i]->speed,
             tapi_cfg_phy_duplex_id2str(mode_candidates[i]->duplex));
    }

    TEST_STEP("Establish a connection between IUT and Tester to check data flow.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Switch both endpoints through common PHY modes in fixed mode.");
    for (i = 0; i < candidates_num; i++)
    {
        mode = mode_candidates[i];

        RING("Switching both endpoints to fixed mode (%d/%s)", mode->speed,
             tapi_cfg_phy_duplex_id2str(mode->duplex));

        te_string_reset(&iut_where);
        te_string_reset(&tst_where);

        te_string_append(&iut_where,
                         "IUT, fixed mode (%d/%s)", mode->speed,
                         tapi_cfg_phy_duplex_id2str(mode->duplex));
        te_string_append(&tst_where,
                         "Tester, fixed mode (%d/%s)", mode->speed,
                         tapi_cfg_phy_duplex_id2str(mode->duplex));

        TEST_SUBSTEP("Try to set one of common PHY mode on IUT");
        rc = net_drv_phy_apply_fixed_mode(iut_rpcs->ta, iut_if->if_name, mode->speed,
                                          mode->duplex);
        if (TE_RC_GET_ERROR(rc) == TE_EINVAL ||
            TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
        {
            WARN("%s: switching is unsupported", iut_where.ptr);
            continue;
        }
        else if (rc != 0)
        {
            TEST_FAIL("Failed to apply fixed PHY mode on IUT");
        }

        TEST_SUBSTEP("Try to set one of common PHY mode on Tester");
        rc = net_drv_phy_apply_fixed_mode(tst_rpcs->ta, tst_if->if_name, mode->speed,
                                          mode->duplex);
        if (TE_RC_GET_ERROR(rc) == TE_EINVAL ||
            TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
        {
            WARN("%s: switching is unsupported", tst_where.ptr);
            continue;
        }
        else if (rc != 0)
        {
            TEST_FAIL("Failed to apply fixed PHY mode on Tester");
        }

        NET_DRV_WAIT_PHY_CHANGE;

        TEST_SUBSTEP("Wait until link comes up on both sides after switching fixed mode.");
        net_drv_wait_up(iut_rpcs->ta, iut_if->if_name);
        net_drv_wait_up(tst_rpcs->ta, tst_if->if_name);

        TEST_SUBSTEP("Check link settings reported by get link ksettings after switching fixed mode.");
        net_drv_phy_check_autoneg_state(iut_where.ptr, iut_rpcs->ta,
                                        iut_if->if_name,
                                        TE_PHY_AUTONEG_OFF);
        net_drv_phy_check_autoneg_state(tst_where.ptr, tst_rpcs->ta,
                                        tst_if->if_name,
                                        TE_PHY_AUTONEG_OFF);
        net_drv_phy_check_lp_autoneg_state(iut_where.ptr, iut_rpcs->ta,
                                           iut_if->if_name,
                                           TE_PHY_AUTONEG_OFF);
        net_drv_phy_check_lp_autoneg_state(tst_where.ptr, tst_rpcs->ta,
                                           tst_if->if_name,
                                           TE_PHY_AUTONEG_OFF);
        net_drv_phy_check_oper_mode(iut_where.ptr, iut_rpcs->ta,
                                    iut_if->if_name,
                                    mode->speed,
                                    mode->duplex);
        net_drv_phy_check_oper_mode(tst_where.ptr, tst_rpcs->ta,
                                    tst_if->if_name,
                                    mode->speed,
                                    mode->duplex);

        TEST_SUBSTEP("Check that traffic flows after switching fixed mode.");
        te_string_reset(&traffic_step);
        te_string_append(&traffic_step,
                         "Checking data flow with fixed mode %s (%d/%s)",
                         mode->name, mode->speed,
                         tapi_cfg_phy_duplex_id2str(mode->duplex));
        net_drv_conn_check(iut_rpcs, iut_s, "IUT socket",
                           tst_rpcs, tst_s, "Tester socket",
                           traffic_step.ptr);
        tested_num++;
    }

    if (tested_num <= 1)
        TEST_SKIP("Too few common PHY modes accepted fixed-mode switching");

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

    te_string_free(&traffic_step);
    te_string_free(&iut_where);
    te_string_free(&tst_where);
    free(mode_candidates);
    net_drv_phy_state_free(&iut_saved);
    net_drv_phy_state_free(&tst_saved);

    TEST_END;
}

/** @} */
