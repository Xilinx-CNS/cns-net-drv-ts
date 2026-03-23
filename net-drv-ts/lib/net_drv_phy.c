/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/** @file
 * @brief Common test API for PHY checks
 *
 * Implementation of helper functions for PHY configuration checks in
 * net driver tests.
 */

/** Log user for this file */
#define TE_LGR_USER "Library"

#include "conf_api.h"
#include "net_drv_phy.h"
#include "net_drv_ts.h"
#include "tapi_test.h"

static te_errno
net_drv_phy_sync_tree(const char *ta, const char *if_name)
{
    return cfg_synchronize_fmt(true, "/agent:%s/interface:%s/phy:",
                               ta, if_name);
}

static bool
net_drv_phy_lp_tree_is_exposed(const char *where, const char *ta,
                               const char *if_name)
{
    cfg_handle *handles = NULL;
    unsigned int num = 0;
    te_errno rc;

    CHECK_RC(net_drv_phy_sync_tree(ta, if_name));

    rc = cfg_find_pattern_fmt(&num, &handles,
                              "/agent:%s/interface:%s/phy:/lp_advertised:*",
                              ta, if_name);
    if (rc == TE_RC(TE_CS, TE_ENOENT))
    {
        WARN("%s: link partner advertised modes are not exposed in PHY tree",
             where);
        return false;
    }
    CHECK_RC(rc);

    free(handles);

    if (num == 0)
    {
        WARN("%s: link partner advertised modes are empty in PHY tree",
             where);
        return false;
    }

    return true;
}

static bool
net_drv_phy_parse_speed_duplex_mode(const char *mode_name, int *speed,
                                    int *duplex)
{
    const char *duplex_suffix;
    unsigned long parsed_speed;
    char *endptr;

    parsed_speed = strtoul(mode_name, &endptr, 10);
    if (endptr == mode_name || parsed_speed > INT_MAX ||
        strncmp(endptr, "base", 4) != 0)
    {
        return false;
    }

    duplex_suffix = strrchr(mode_name, '_');
    if (duplex_suffix == NULL)
        return false;

    if (strcmp(duplex_suffix, "_Full") == 0)
        *duplex = TE_PHY_DUPLEX_FULL;
    else if (strcmp(duplex_suffix, "_Half") == 0)
        *duplex = TE_PHY_DUPLEX_HALF;
    else if (strcmp(mode_name, "10000baseR_FEC") == 0)
        *duplex = TE_PHY_DUPLEX_FULL;
    else
        return false;

    *speed = (int)parsed_speed;
    return true;
}

/* See description in net_drv_phy.h */
void
net_drv_phy_state_free(net_drv_phy_saved_state *state)
{
    unsigned int i;

    if (state->modes != NULL)
    {
        for (i = 0; i < state->modes_num; i++)
            free(state->modes[i].name);
    }

    free(state->modes);
    memset(state, 0, sizeof(*state));
}

/* See description in net_drv_phy.h */
te_errno
net_drv_phy_state_save(const char *ta, const char *if_name,
                       net_drv_phy_saved_state *state)
{
    cfg_handle *handles = NULL;
    te_errno rc;
    unsigned int i;

    memset(state, 0, sizeof(*state));

    rc = tapi_cfg_phy_autoneg_get(ta, if_name, &state->autoneg);
    if (rc != 0)
        return rc;

    rc = tapi_cfg_phy_speed_admin_get(ta, if_name, &state->speed_admin);
    if (rc != 0)
        return rc;

    rc = tapi_cfg_phy_duplex_admin_get(ta, if_name, &state->duplex_admin);
    if (rc != 0)
        return rc;

    rc = cfg_find_pattern_fmt(&state->modes_num, &handles,
                              "/agent:%s/interface:%s/phy:/mode:*",
                              ta, if_name);
    if (rc != 0)
        return rc;

    if (state->modes_num != 0)
        state->modes = TE_ALLOC(state->modes_num * sizeof(*state->modes));

    for (i = 0; i < state->modes_num; i++)
    {
        rc = cfg_get_inst_name(handles[i], &state->modes[i].name);
        if (rc != 0)
            break;

        rc = tapi_cfg_phy_mode_adv_get(ta, if_name, state->modes[i].name,
                                       &state->modes[i].advertised);
        if (rc != 0)
            break;

        if (!net_drv_phy_parse_speed_duplex_mode(state->modes[i].name,
                                                 &state->modes[i].speed,
                                                 &state->modes[i].duplex))
        {
            state->modes[i].speed = TE_PHY_SPEED_UNKNOWN;
            state->modes[i].duplex = TE_PHY_DUPLEX_UNKNOWN;
        }
    }

    free(handles);

    if (rc != 0)
    {
        net_drv_phy_state_free(state);
        return rc;
    }

    state->saved = true;
    return 0;
}

/* See description in net_drv_phy.h */
te_errno
net_drv_phy_state_restore(const char *ta, const char *if_name,
                          const net_drv_phy_saved_state *state)
{
    bool have_changes = false;
    bool current_advertised;
    int current_autoneg;
    int current_duplex;
    int current_speed;
    unsigned int i;
    te_errno rc;

    rc = net_drv_phy_sync_tree(ta, if_name);
    if (rc != 0)
        return rc;

    /*
     * Once a local command sequence is started, a regular GET will fail with
     * TE_EACCES until commit, so check whether changes are needed first.
     */
    rc = tapi_cfg_phy_autoneg_get(ta, if_name, &current_autoneg);
    if (rc != 0)
        return rc;

    if (state->autoneg == TE_PHY_AUTONEG_OFF)
    {
        rc = tapi_cfg_phy_speed_admin_get(ta, if_name, &current_speed);
        if (rc != 0)
            return rc;

        if (current_speed != state->speed_admin)
            have_changes = true;

        rc = tapi_cfg_phy_duplex_admin_get(ta, if_name, &current_duplex);
        if (rc != 0)
            return rc;

        if (current_duplex != state->duplex_admin)
            have_changes = true;
    }

    if (current_autoneg != state->autoneg)
        have_changes = true;

    for (i = 0; i < state->modes_num; i++)
    {
        if (!net_drv_phy_is_link_mode(&state->modes[i]))
            continue;

        rc = tapi_cfg_phy_mode_adv_get(ta, if_name, state->modes[i].name,
                                       &current_advertised);
        if (rc != 0)
            return rc;

        if (current_advertised != state->modes[i].advertised)
        {
            have_changes = true;
            break;
        }
    }

    if (!have_changes)
        return 0;

    rc = tapi_cfg_phy_autoneg_set(ta, if_name, state->autoneg);
    if (rc != 0)
        return rc;

    if (state->autoneg == TE_PHY_AUTONEG_OFF)
    {
        rc = tapi_cfg_phy_speed_admin_set(ta, if_name,
                                          state->speed_admin);
        if (rc != 0)
            return rc;

        rc = tapi_cfg_phy_duplex_admin_set(ta, if_name,
                                           state->duplex_admin);
        if (rc != 0)
            return rc;
    }

    for (i = 0; i < state->modes_num; i++)
    {
        if (!net_drv_phy_is_link_mode(&state->modes[i]))
            continue;

        rc = tapi_cfg_phy_mode_adv_set(ta, if_name, state->modes[i].name,
                                       state->modes[i].advertised);
        if (rc != 0)
            return rc;
    }

    rc = tapi_cfg_phy_commit(ta, if_name);

    return rc;
}

/* See description in net_drv_phy.h */
void
net_drv_phy_state_save_or_skip(const char *who, const char *ta,
                               const char *if_name,
                               net_drv_phy_saved_state *state)
{
    te_errno rc;

    rc = net_drv_phy_state_save(ta, if_name, state);
    if (rc == 0)
        return;

    if (TE_RC_GET_ERROR(rc) == TE_ENOENT ||
        TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
    {
        TEST_SKIP("PHY link settings are not exposed on %s", who);
    }

    TEST_VERDICT("Failed to save %s PHY state, rc=%r", who, rc);
}

/* See description in net_drv_phy.h */
const net_drv_phy_mode_info *
net_drv_phy_state_find_mode_by_name(const net_drv_phy_saved_state *state,
                                    const char *name)
{
    unsigned int i;

    for (i = 0; i < state->modes_num; i++)
    {
        if (strcmp(state->modes[i].name, name) == 0)
            return &state->modes[i];
    }

    return NULL;
}

/* See description in net_drv_phy.h */
bool
net_drv_phy_is_link_mode(const net_drv_phy_mode_info *mode)
{
    return mode->speed != TE_PHY_SPEED_UNKNOWN &&
           mode->duplex != TE_PHY_DUPLEX_UNKNOWN;
}

/* See description in net_drv_phy.h */
te_errno
net_drv_phy_apply_autoneg_modes(const char *ta, const char *if_name,
                                const net_drv_phy_saved_state *state,
                                const char *mode1_name,
                                const char *mode2_name)
{
    bool have_changes = false;
    bool current_advertised;
    bool should_advertise;
    int current_autoneg;
    unsigned int i;
    te_errno rc;

    /*
     * Once a local command sequence is started, a regular GET will fail with
     * TE_EACCES until commit, so check whether changes are needed first.
     */
    rc = tapi_cfg_phy_autoneg_get(ta, if_name, &current_autoneg);
    if (rc != 0)
        return rc;

    if (current_autoneg != TE_PHY_AUTONEG_ON)
        have_changes = true;

    for (i = 0; i < state->modes_num; i++)
    {
        if (!net_drv_phy_is_link_mode(&state->modes[i]))
            continue;

        should_advertise = (strcmp(state->modes[i].name, mode1_name) == 0) ||
                           (mode2_name != NULL &&
                            strcmp(state->modes[i].name, mode2_name) == 0);

        rc = tapi_cfg_phy_mode_adv_get(ta, if_name, state->modes[i].name,
                                       &current_advertised);
        if (rc != 0)
            return rc;

        if (current_advertised != should_advertise)
        {
            have_changes = true;
            break;
        }
    }

    if (!have_changes)
        return 0;

    for (i = 0; i < state->modes_num; i++)
    {
        if (!net_drv_phy_is_link_mode(&state->modes[i]))
            continue;

        should_advertise = (strcmp(state->modes[i].name, mode1_name) == 0) ||
                           (mode2_name != NULL &&
                            strcmp(state->modes[i].name, mode2_name) == 0);

        rc = tapi_cfg_phy_mode_adv_set(ta, if_name, state->modes[i].name,
                                       should_advertise);
        if (rc != 0)
            return rc;
    }

    rc = tapi_cfg_phy_autoneg_set(ta, if_name, TE_PHY_AUTONEG_ON);
    if (rc != 0)
        return rc;

    return tapi_cfg_phy_commit(ta, if_name);
}

/* See description in net_drv_phy.h */
te_errno
net_drv_phy_apply_fixed_mode(const char *ta, const char *if_name,
                             int speed, int duplex)
{
    bool have_changes = false;
    int current_autoneg;
    int current_duplex;
    int current_speed;
    te_errno rc;

    rc = tapi_cfg_phy_autoneg_get(ta, if_name, &current_autoneg);
    if (rc != 0)
        return rc;

    rc = tapi_cfg_phy_speed_admin_get(ta, if_name, &current_speed);
    if (rc != 0)
        return rc;

    rc = tapi_cfg_phy_duplex_admin_get(ta, if_name, &current_duplex);
    if (rc != 0)
        return rc;

    if (current_autoneg != TE_PHY_AUTONEG_OFF)
    {
        rc = tapi_cfg_phy_autoneg_set(ta, if_name, TE_PHY_AUTONEG_OFF);
        if (rc != 0)
            return rc;

        have_changes = true;
    }

    if (current_speed != speed)
    {
        rc = tapi_cfg_phy_speed_admin_set(ta, if_name, speed);
        if (rc != 0)
            return rc;

        have_changes = true;
    }

    if (current_duplex != duplex)
    {
        rc = tapi_cfg_phy_duplex_admin_set(ta, if_name, duplex);
        if (rc != 0)
            return rc;

        have_changes = true;
    }

    if (!have_changes)
        return 0;

    return tapi_cfg_phy_commit(ta, if_name);
}

/* See description in net_drv_phy.h */
void
net_drv_phy_check_autoneg_state(const char *where, const char *ta,
                                const char *if_name, int expected)
{
    int autoneg;

    CHECK_RC(tapi_cfg_phy_autoneg_get(ta, if_name, &autoneg));
    if (autoneg != expected)
    {
        ERROR_VERDICT("%s: unexpected autoneg state %s instead of %s",
                      where, tapi_cfg_phy_autoneg_id2str(autoneg),
                      tapi_cfg_phy_autoneg_id2str(expected));
    }
}

/* See description in net_drv_phy.h */
void
net_drv_phy_check_lp_autoneg_state(const char *where, const char *ta,
                                   const char *if_name, int expected)
{
    int lp_autoneg;

    if (!net_drv_phy_lp_tree_is_exposed(where, ta, if_name))
        return;

    CHECK_RC(net_drv_phy_sync_tree(ta, if_name));
    CHECK_RC(tapi_cfg_phy_autoneg_lp_adv_get(ta, if_name, &lp_autoneg));
    if (lp_autoneg != expected)
    {
        ERROR_VERDICT("%s: unexpected link partner autoneg state %s "
                      "instead of %s",
                      where, tapi_cfg_phy_autoneg_id2str(lp_autoneg),
                      tapi_cfg_phy_autoneg_id2str(expected));
    }
}

/* See description in net_drv_phy.h */
void
net_drv_phy_check_oper_mode(const char *where, const char *ta,
                            const char *if_name, int expected_speed,
                            int expected_duplex)
{
    int duplex;
    int speed;

    CHECK_RC(tapi_cfg_phy_mode_oper_get(ta, if_name, &speed, &duplex));
    if (speed != expected_speed || duplex != expected_duplex)
    {
        ERROR_VERDICT("%s: unexpected operational mode %d/%s instead of %d/%s",
                      where, speed, tapi_cfg_phy_duplex_id2str(duplex),
                      expected_speed,
                      tapi_cfg_phy_duplex_id2str(expected_duplex));
    }
}

/* See description in net_drv_phy.h */
void
net_drv_phy_check_local_mode_advertised(const char *where, const char *ta,
                                        const char *if_name,
                                        const char *mode_name,
                                        bool expected)
{
    bool advertised;

    CHECK_RC(tapi_cfg_phy_mode_adv_get(ta, if_name, mode_name, &advertised));
    if (advertised != expected)
    {
        ERROR_VERDICT("%s: exact mode %s is%s advertised",
                      where, mode_name, advertised ? "" : " not");
    }
}

/* See description in net_drv_phy.h */
void
net_drv_phy_check_partner_mode_advertised(const char *where, const char *ta,
                                          const char *if_name,
                                          const char *mode_name,
                                          bool expected)
{
    bool advertised;

    if (!net_drv_phy_lp_tree_is_exposed(where, ta, if_name))
        return;

    CHECK_RC(net_drv_phy_sync_tree(ta, if_name));
    CHECK_RC(tapi_cfg_phy_lp_advertised(ta, if_name, mode_name, &advertised));
    if (advertised != expected)
    {
        ERROR("Unexpected link partner advertised state for %s", mode_name);
        ERROR_VERDICT("%s: link partner %s requested mode",
                      where, advertised ? "advertises" : "does not advertise");
    }
}
