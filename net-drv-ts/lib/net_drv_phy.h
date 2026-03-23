/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/** @file
 * @brief Common test API for PHY checks
 *
 * Declarations of helper functions for PHY configuration checks in
 * net driver tests.
 */

#ifndef __TS_NET_DRV_PHY_H__
#define __TS_NET_DRV_PHY_H__

#include "te_config.h"
#include "te_defs.h"

#include "te_errno.h"

/** Wait until PHY changes are applied. */
#define NET_DRV_WAIT_PHY_CHANGE \
    te_motivated_sleep(2, "wait after PHY changes")

/**
 * Description of one exact PHY mode exposed under @c /phy:/mode:*.
 */
typedef struct net_drv_phy_mode_info {
    char *name; /**< Exact PHY mode name. */
    bool advertised; /**< Whether this mode is currently advertised. */
    int speed; /**< Parsed speed or @c TE_PHY_SPEED_UNKNOWN. */
    int duplex; /**< Parsed duplex or @c TE_PHY_DUPLEX_UNKNOWN. */
} net_drv_phy_mode_info;

/**
 * Saved PHY configuration for later restoration.
 */
typedef struct net_drv_phy_saved_state {
    int autoneg; /**< Autonegotiation state. */
    int speed_admin; /**< Administrative speed. */
    int duplex_admin; /**< Administrative duplex. */
    net_drv_phy_mode_info *modes; /**< Exact PHY modes. */
    unsigned int modes_num; /**< Number of entries in @p modes. */
    bool saved; /**< Whether the state was saved successfully. */
} net_drv_phy_saved_state;

/**
 * Release memory allocated inside saved PHY state.
 *
 * @param state     State to release.
 */
extern void net_drv_phy_state_free(net_drv_phy_saved_state *state);

/**
 * Save current PHY configuration under @c /phy:/mode:*.
 *
 * @param ta        Test Agent name.
 * @param if_name   Interface name.
 * @param state     Where to save PHY configuration.
 *
 * @return Status code.
 */
extern te_errno net_drv_phy_state_save(const char *ta, const char *if_name,
                                       net_drv_phy_saved_state *state);

/**
 * Restore PHY configuration.
 *
 * Autonegotiation state is always restored. Administrative speed and duplex
 * are restored only when saved autonegotiation state is off. Saved advertised
 * states are restored only for exact PHY link modes with known speed and
 * duplex; other @c /phy:/mode:* entries are left unchanged.
 *
 * @param ta        Test Agent name.
 * @param if_name   Interface name.
 * @param state     Previously saved PHY configuration.
 *
 * @return Status code.
 */
extern te_errno net_drv_phy_state_restore(
    const char *ta, const char *if_name,
    const net_drv_phy_saved_state *state);

/**
 * Save current PHY configuration or terminate the test with
 * @c TEST_SKIP()/@c TEST_VERDICT() if the configuration cannot be accessed.
 *
 * @param who       Human-readable endpoint name for log messages.
 * @param ta        Test Agent name.
 * @param if_name   Interface name.
 * @param state     Where to save PHY configuration.
 */
extern void net_drv_phy_state_save_or_skip(const char *who, const char *ta,
                                           const char *if_name,
                                           net_drv_phy_saved_state *state);

/**
 * Find exact PHY mode by name in saved PHY state.
 *
 * @param state     Saved PHY state.
 * @param name      Exact PHY mode name to find.
 *
 * @return Pointer to mode description, or @c NULL if it is absent.
 */
extern const net_drv_phy_mode_info *net_drv_phy_state_find_mode_by_name(
    const net_drv_phy_saved_state *state, const char *name);

/**
 * Check whether PHY mode description corresponds to a link mode.
 *
 * @param mode      PHY mode description.
 *
 * @return @c true if both speed and duplex are known for this mode.
 */
extern bool net_drv_phy_is_link_mode(
    const net_drv_phy_mode_info *mode);

/**
 * Enable autonegotiation and advertise one or two exact PHY modes.
 *
 * All exact PHY link modes with known speed and duplex not listed in
 * arguments are disabled. Other @c /phy:/mode:* entries such as pause or
 * FEC capabilities are left unchanged.
 *
 * @param ta            Test Agent name.
 * @param if_name       Interface name.
 * @param state         Saved PHY state used to enumerate available modes.
 * @param mode1_name    First exact PHY mode to advertise.
 * @param mode2_name    Optional second exact PHY mode to advertise.
 *
 * @return Status code.
 */
extern te_errno net_drv_phy_apply_autoneg_modes(
    const char *ta, const char *if_name,
    const net_drv_phy_saved_state *state,
    const char *mode1_name, const char *mode2_name);

/**
 * Disable autonegotiation and configure a fixed administrative PHY mode.
 *
 * @param ta        Test Agent name.
 * @param if_name   Interface name.
 * @param speed     Administrative speed to set.
 * @param duplex    Administrative duplex to set.
 *
 * @return Status code.
 */
extern te_errno net_drv_phy_apply_fixed_mode(const char *ta,
                                             const char *if_name,
                                             int speed, int duplex);

/**
 * Check local autonegotiation state and verdict on mismatch.
 *
 * @param where         Human-readable endpoint description.
 * @param ta            Test Agent name.
 * @param if_name       Interface name.
 * @param expected      Expected autonegotiation state.
 */
extern void net_drv_phy_check_autoneg_state(const char *where,
                                            const char *ta,
                                            const char *if_name,
                                            int expected);

/**
 * Check link partner autonegotiation state and verdict on mismatch.
 *
 * If link partner advertised modes are not exposed in PHY tree for the
 * endpoint, the helper logs a warning and skips the check.
 *
 * @param where         Human-readable endpoint description.
 * @param ta            Test Agent name.
 * @param if_name       Interface name.
 * @param expected      Expected partner autonegotiation state.
 */
extern void net_drv_phy_check_lp_autoneg_state(const char *where,
                                               const char *ta,
                                               const char *if_name,
                                               int expected);

/**
 * Check operational PHY mode and verdict on mismatch.
 *
 * @param where             Human-readable endpoint description.
 * @param ta                Test Agent name.
 * @param if_name           Interface name.
 * @param expected_speed    Expected operational speed.
 * @param expected_duplex   Expected operational duplex.
 */
extern void net_drv_phy_check_oper_mode(const char *where, const char *ta,
                                        const char *if_name,
                                        int expected_speed,
                                        int expected_duplex);

/**
 * Check whether a local exact PHY mode is advertised and verdict on mismatch.
 *
 * @param where         Human-readable endpoint description.
 * @param ta            Test Agent name.
 * @param if_name       Interface name.
 * @param mode_name     Exact PHY mode name.
 * @param expected      Expected advertised state.
 */
extern void net_drv_phy_check_local_mode_advertised(
    const char *where, const char *ta, const char *if_name,
    const char *mode_name, bool expected);

/**
 * Check whether an exact PHY mode is advertised by the link partner.
 *
 * If link partner advertised modes are not exposed in PHY tree for the
 * endpoint, the helper logs a warning and skips the check.
 *
 * @param where         Human-readable endpoint description.
 * @param ta            Test Agent name.
 * @param if_name       Interface name.
 * @param mode_name     Exact PHY mode name.
 * @param expected      Expected advertised state.
 */
extern void net_drv_phy_check_partner_mode_advertised(
    const char *where, const char *ta, const char *if_name,
    const char *mode_name, bool expected);

#endif /* !__TS_NET_DRV_PHY_H__ */
