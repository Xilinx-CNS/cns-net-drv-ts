/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-autoneg_advertise Check autonegotiation and advertised modes.
 * @ingroup ethtool
 * @{
 *
 * @objective Check that autonegotiation selects the preferred common exact
 *            PHY mode and renegotiates to a lower-priority common mode after
 *            advertised modes are changed on one side.
 *
 * @param env            Testing environment:
 *                       - @c env.peer2peer.an_iut_change
 *                       - @c env.peer2peer.an_tst_change
 * @param if_down        If @c TRUE, restart IUT interface after PHY changes.
 *
 * @author Denis Pryazhennikov <denis.pryazhennikov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "ethtool/autoneg_advertise"

#include "net_drv_test.h"
#include "net_drv_phy.h"
#include "tapi_cfg_base.h"

static int
pair_priority_cmp(int speed1, int duplex1, int speed2, int duplex2)
{
    if (speed1 != speed2)
        return (speed1 > speed2) ? 1 : -1;

    if (duplex1 != duplex2)
    {
        if (duplex1 == TE_PHY_DUPLEX_FULL)
            return 1;
        if (duplex2 == TE_PHY_DUPLEX_FULL)
            return -1;
    }

    return 0;
}

static void
update_common_nodes(const net_drv_phy_mode_info *new_mode,
                    bool *have_preferred,
                    const net_drv_phy_mode_info **preferred_mode,
                    bool *have_fallback,
                    const net_drv_phy_mode_info **fallback_mode)
{
    if (*have_preferred &&
        pair_priority_cmp(new_mode->speed, new_mode->duplex,
                          (*preferred_mode)->speed,
                          (*preferred_mode)->duplex) == 0)
    {
        return;
    }

    if (*have_fallback &&
        pair_priority_cmp(new_mode->speed, new_mode->duplex,
                          (*fallback_mode)->speed,
                          (*fallback_mode)->duplex) == 0)
    {
        return;
    }

    if (!*have_preferred ||
        pair_priority_cmp(new_mode->speed, new_mode->duplex,
                          (*preferred_mode)->speed,
                          (*preferred_mode)->duplex) > 0)
    {
        if (*have_preferred)
        {
            *fallback_mode = *preferred_mode;
            *have_fallback = true;
        }

        *preferred_mode = new_mode;
        *have_preferred = true;
        return;
    }

    if (!*have_fallback ||
        pair_priority_cmp(new_mode->speed, new_mode->duplex,
                          (*fallback_mode)->speed,
                          (*fallback_mode)->duplex) > 0)
    {
        *have_fallback = true;
        *fallback_mode = new_mode;
    }
}

static bool
find_common_advertised_modes(const net_drv_phy_saved_state *iut_state,
                             const net_drv_phy_saved_state *tst_state,
                             const net_drv_phy_mode_info **pref,
                             const net_drv_phy_mode_info **fallback)
{
    const net_drv_phy_mode_info *peer_mode;
    bool have_fallback = false;
    bool have_pref = false;
    unsigned int i;

    for (i = 0; i < iut_state->modes_num; i++)
    {
        if (!net_drv_phy_is_link_mode(&iut_state->modes[i]) ||
            !iut_state->modes[i].advertised)
        {
            continue;
        }

        peer_mode = net_drv_phy_state_find_mode_by_name(
            tst_state, iut_state->modes[i].name);
        if (peer_mode == NULL || !net_drv_phy_is_link_mode(peer_mode) ||
            !peer_mode->advertised)
        {
            continue;
        }

        update_common_nodes(&iut_state->modes[i], &have_pref, pref,
                            &have_fallback, fallback);
    }

    return have_pref && have_fallback;
}

static void
test_phy_apply_autoneg_modes(const char *ta, const char *if_name,
                             const net_drv_phy_saved_state *state,
                             const char *pref_mode, const char *fallback_mode,
                             const char *where)
{
    te_errno rc;

    rc = net_drv_phy_apply_autoneg_modes(ta, if_name, state, pref_mode,
                                         fallback_mode);
    if (TE_RC_GET_ERROR(rc) == TE_EINVAL ||
        TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
    {
        TEST_SKIP("Changing advertised PHY modes is not supported on %s",
                  where);
    }
    else if (rc != 0)
    {
        TEST_FAIL("Failed to change advertised PHY modes on %s", where);
    }
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

    const net_drv_phy_mode_info *fallback_mode = NULL;
    const net_drv_phy_mode_info *pref_mode = NULL;
    net_drv_phy_saved_state iut_phy_saved = {};
    net_drv_phy_saved_state *changed_saved = NULL;
    net_drv_phy_saved_state tst_phy_saved = {};
    int iut_s = -1;
    int tst_s = -1;
    bool if_down;

    const struct if_nameindex *changed_if = NULL;
    rcf_rpc_server *changed_rpcs = NULL;

    const struct if_nameindex *peer_if = NULL;
    rcf_rpc_server *peer_rpcs = NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_PCO(changed_rpcs);
    TEST_GET_PCO(peer_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_IF(changed_if);
    TEST_GET_IF(peer_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BOOL_PARAM(if_down);

    changed_saved = (changed_rpcs == iut_rpcs) ? &iut_phy_saved :
                                                 &tst_phy_saved;

    TEST_STEP("Save current PHY configuration on both endpoints.");
    net_drv_phy_state_save_or_skip("IUT", iut_rpcs->ta, iut_if->if_name,
                                   &iut_phy_saved);
    net_drv_phy_state_save_or_skip("Tester", tst_rpcs->ta, tst_if->if_name,
                                   &tst_phy_saved);

    TEST_STEP("Choose two common advertised PHY modes with different priorities.");
    if (!find_common_advertised_modes(&iut_phy_saved, &tst_phy_saved,
                                      &pref_mode, &fallback_mode))
    {
        TEST_SKIP("At least two common advertised PHY modes with different priorities are required");
    }

    if (pair_priority_cmp(pref_mode->speed, pref_mode->duplex,
                          fallback_mode->speed, fallback_mode->duplex) <= 0)
        TEST_VERDICT("Failed to choose preferred and lower-priority PHY modes");

    RING("Preferred PHY mode: %s (%d/%s)",
         pref_mode->name, pref_mode->speed,
         tapi_cfg_phy_duplex_id2str(pref_mode->duplex));
    RING("Lower-priority PHY mode: %s (%d/%s)",
         fallback_mode->name, fallback_mode->speed,
         tapi_cfg_phy_duplex_id2str(fallback_mode->duplex));

    TEST_STEP("Establish a connection between IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Advertise preferred and lower-priority PHY modes on both sides.");
    test_phy_apply_autoneg_modes(iut_rpcs->ta, iut_if->if_name, &iut_phy_saved,
                                 pref_mode->name, fallback_mode->name, "IUT");
    test_phy_apply_autoneg_modes(tst_rpcs->ta, tst_if->if_name, &tst_phy_saved,
                                 pref_mode->name, fallback_mode->name,
                                 "Tester");
    NET_DRV_WAIT_PHY_CHANGE;

    TEST_STEP("Wait until link comes up on both sides.");
    net_drv_wait_up(iut_rpcs->ta, iut_if->if_name);
    net_drv_wait_up(tst_rpcs->ta, tst_if->if_name);

    TEST_STEP("Check local autonegotiation state on both sides with the initial advertised set.");
    net_drv_phy_check_autoneg_state("IUT initial", iut_rpcs->ta,
                                    iut_if->if_name, TE_PHY_AUTONEG_ON);
    net_drv_phy_check_autoneg_state("Tester initial", tst_rpcs->ta,
                                    tst_if->if_name, TE_PHY_AUTONEG_ON);
    TEST_STEP("Check link partner autonegotiation state on both sides with the initial advertised set.");
    net_drv_phy_check_lp_autoneg_state("IUT initial", iut_rpcs->ta,
                                       iut_if->if_name, TE_PHY_AUTONEG_ON);
    net_drv_phy_check_lp_autoneg_state("Tester initial", tst_rpcs->ta,
                                       tst_if->if_name, TE_PHY_AUTONEG_ON);

    TEST_STEP("Check locally advertised exact PHY modes with the initial advertised set.");
    net_drv_phy_check_local_mode_advertised("IUT initial local preferred PHY",
                                            iut_rpcs->ta, iut_if->if_name,
                                            pref_mode->name, true);
    net_drv_phy_check_local_mode_advertised("IUT initial local lower-priority PHY",
                                            iut_rpcs->ta, iut_if->if_name,
                                            fallback_mode->name, true);
    net_drv_phy_check_local_mode_advertised("Tester initial local preferred PHY",
                                            tst_rpcs->ta, tst_if->if_name,
                                            pref_mode->name, true);
    net_drv_phy_check_local_mode_advertised("Tester initial local lower-priority PHY",
                                            tst_rpcs->ta, tst_if->if_name,
                                            fallback_mode->name, true);

    TEST_STEP("Check link partner advertised exact modes with the initial advertised set.");
    net_drv_phy_check_partner_mode_advertised("IUT initial partner preferred PHY",
                                              iut_rpcs->ta, iut_if->if_name,
                                              pref_mode->name, true);
    net_drv_phy_check_partner_mode_advertised("IUT initial partner lower-priority PHY",
                                              iut_rpcs->ta, iut_if->if_name,
                                              fallback_mode->name, true);
    net_drv_phy_check_partner_mode_advertised("Tester initial partner preferred PHY",
                                              tst_rpcs->ta, tst_if->if_name,
                                              pref_mode->name, true);
    net_drv_phy_check_partner_mode_advertised("Tester initial partner lower-priority PHY",
                                              tst_rpcs->ta, tst_if->if_name,
                                              fallback_mode->name, true);

    TEST_STEP("Check that the preferred PHY mode is negotiated with the initial advertised set.");
    net_drv_phy_check_oper_mode("IUT initial", iut_rpcs->ta,
                                iut_if->if_name, pref_mode->speed,
                                pref_mode->duplex);
    net_drv_phy_check_oper_mode("Tester initial", tst_rpcs->ta,
                                tst_if->if_name, pref_mode->speed,
                                pref_mode->duplex);

    TEST_STEP("Check that traffic flows with the initial advertised set.");
    net_drv_conn_check(iut_rpcs, iut_s, "IUT socket",
                       tst_rpcs, tst_s, "Tester socket",
                       "Checking data flow with the initial advertised set");

    TEST_STEP("Remove the preferred PHY mode from the advertised set on the changed side.");
    test_phy_apply_autoneg_modes(changed_rpcs->ta, changed_if->if_name,
                                 changed_saved, fallback_mode->name, NULL,
                                 "one of sides");

    if (if_down)
    {
        TEST_STEP("Bring the IUT interface down.");
        CHECK_RC(tapi_cfg_base_if_down(iut_rpcs->ta, iut_if->if_name));
        CFG_WAIT_CHANGES;
        TEST_STEP("Bring the IUT interface up.");
        CHECK_RC(tapi_cfg_base_if_up(iut_rpcs->ta, iut_if->if_name));
        CFG_WAIT_CHANGES;
    }
    else
    {
        NET_DRV_WAIT_PHY_CHANGE;
    }

    TEST_STEP("Wait until link comes up on both sides.");
    net_drv_wait_up(iut_rpcs->ta, iut_if->if_name);
    net_drv_wait_up(tst_rpcs->ta, tst_if->if_name);

    TEST_STEP("Check local autonegotiation state on both sides.");
    net_drv_phy_check_autoneg_state("IUT after advertise change",
                                    iut_rpcs->ta, iut_if->if_name,
                                    TE_PHY_AUTONEG_ON);
    net_drv_phy_check_autoneg_state("Tester after advertise change",
                                    tst_rpcs->ta, tst_if->if_name,
                                    TE_PHY_AUTONEG_ON);

    TEST_STEP("Check link partner autonegotiation state on both sides.");
    net_drv_phy_check_lp_autoneg_state("IUT after advertise change",
                                       iut_rpcs->ta, iut_if->if_name,
                                       TE_PHY_AUTONEG_ON);
    net_drv_phy_check_lp_autoneg_state("Tester after advertise change",
                                       tst_rpcs->ta, tst_if->if_name,
                                       TE_PHY_AUTONEG_ON);

    TEST_STEP("Check locally advertised exact modes.");
    net_drv_phy_check_local_mode_advertised("Changed side local preferred PHY",
                                            changed_rpcs->ta,
                                            changed_if->if_name,
                                            pref_mode->name, false);
    net_drv_phy_check_local_mode_advertised("Changed side local lower-priority PHY",
                                            changed_rpcs->ta,
                                            changed_if->if_name,
                                            fallback_mode->name, true);
    net_drv_phy_check_local_mode_advertised("Peer side local preferred PHY",
                                            peer_rpcs->ta,
                                            peer_if->if_name,
                                            pref_mode->name, true);
    net_drv_phy_check_local_mode_advertised("Peer side local lower-priority PHY",
                                            peer_rpcs->ta,
                                            peer_if->if_name,
                                            fallback_mode->name, true);

    TEST_STEP("Check link partner advertised exact modes.");
    net_drv_phy_check_partner_mode_advertised("Changed side partner preferred PHY",
                                              changed_rpcs->ta,
                                              changed_if->if_name,
                                              pref_mode->name, true);
    net_drv_phy_check_partner_mode_advertised("Changed side partner lower-priority PHY",
                                              changed_rpcs->ta,
                                              changed_if->if_name,
                                              fallback_mode->name, true);
    net_drv_phy_check_partner_mode_advertised("Peer side partner preferred PHY",
                                              peer_rpcs->ta,
                                              peer_if->if_name,
                                              pref_mode->name, false);
    net_drv_phy_check_partner_mode_advertised("Peer side partner lower-priority PHY",
                                              peer_rpcs->ta,
                                              peer_if->if_name,
                                              fallback_mode->name, true);

    TEST_STEP("Check that both sides renegotiated to the lower-priority PHY mode.");
    net_drv_phy_check_oper_mode("IUT after advertise change",
                                iut_rpcs->ta, iut_if->if_name,
                                fallback_mode->speed, fallback_mode->duplex);
    net_drv_phy_check_oper_mode("Tester after advertise change",
                                tst_rpcs->ta, tst_if->if_name,
                                fallback_mode->speed, fallback_mode->duplex);

    TEST_STEP("Check that traffic still flows.");
    net_drv_conn_check(iut_rpcs, iut_s, "IUT socket",
                       tst_rpcs, tst_s, "Tester socket",
                       "Checking data flow after changing advertised modes");

    TEST_SUCCESS;

cleanup:
    if (iut_phy_saved.saved)
    {
        CLEANUP_CHECK_RC(net_drv_phy_state_restore(iut_rpcs->ta,
                                                   iut_if->if_name,
                                                   &iut_phy_saved));
    }
    if (tst_phy_saved.saved)
    {
        CLEANUP_CHECK_RC(net_drv_phy_state_restore(tst_rpcs->ta,
                                                   tst_if->if_name,
                                                   &tst_phy_saved));
    }
    NET_DRV_WAIT_PHY_CHANGE;
    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);
    if (iut_phy_saved.saved || tst_phy_saved.saved)
    {
        net_drv_wait_up_gen(iut_rpcs->ta, iut_if->if_name, true);
        net_drv_wait_up_gen(tst_rpcs->ta, tst_if->if_name, true);
    }

    net_drv_phy_state_free(&iut_phy_saved);
    net_drv_phy_state_free(&tst_phy_saved);

    TEST_END;
}

/** @} */
