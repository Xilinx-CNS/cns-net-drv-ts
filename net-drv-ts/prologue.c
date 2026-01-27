/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/**
 * @defgroup prologue Test Suite prologue
 * @ingroup net_drv_tests
 * @{
 *
 * @objective Prepare configuration.
 *
 * @param env               Testing environment:
 *                          - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

/** Logging subsystem entity name */
#define TE_TEST_NAME    "prologue"

#include "net_drv_test.h"

#include "tapi_cfg_base.h"
#include "tapi_cfg_if.h"
#include "tapi_cfg_if_chan.h"
#include "tapi_cfg_if_rss.h"
#include "tapi_cfg_net.h"
#include "tapi_cfg_modules.h"
#include "tapi_cfg_pci.h"
#include "tapi_sh_env.h"
#include "tapi_cfg_pci.h"
#include "tapi_reqs.h"
#include "tapi_tags.h"

#define DEF_STR_LEN 1024

static te_errno
load_required_modules(const char *ta, void *cookie)
{
    te_errno rc;
    char *driver = NULL;
    char *net_driver = NULL;
    bool is_net_driver_needed = false;
    bool unloadable = true;

    UNUSED(cookie);

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_NET, &driver);
    if (rc != 0)
        goto cleanup;

    if (driver != NULL)
    {
        rc = tapi_cfg_module_add(ta, driver, false);
        if (rc == 0)
            unloadable = net_drv_driver_unloadable(ta, driver);
        rc = 0;

        if (!unloadable)
        {
            WARN("Driver %s cannot be reloaded", driver);
            goto cleanup;
        }

        net_driver = net_drv_driver_name(ta);
        if (strcmp(driver, net_driver) != 0)
            is_net_driver_needed = true;

        if (strcmp_start("sfc", driver) == 0)
        {
            rc = tapi_cfg_module_add_from_ta_dir_or_fallback(ta, "sfc", TRUE);
        }
        else if (strcmp(NET_DRV_X3_DRIVER_NAME, driver) == 0)
        {
            /**
             * On new kernels auxiliary module is not necessary, so in case of
             * failure let's try to go forth.
             */
            (void)tapi_cfg_module_add_from_ta_dir(ta, "auxiliary", TRUE);
            rc = tapi_cfg_module_add_from_ta_dir(ta, NET_DRV_X3_DRIVER_NAME,
                                                 TRUE);
        }
        else
        {
            rc = tapi_cfg_module_add_from_ta_dir_or_fallback(ta, driver, FALSE);
            if (rc != 0)
                goto cleanup;

            if (is_net_driver_needed)
            {
                rc = tapi_cfg_module_add_from_ta_dir_or_fallback(ta,
                                                                 net_driver,
                                                                 FALSE);
                if (rc != 0)
                    goto cleanup;
            }
        }
    }

cleanup:
    free(driver);
    free(net_driver);
    return rc;
}

static te_errno
disable_fw_lldp(cfg_net_t *, cfg_net_node_t *, const char *,
                cfg_oid *oid, void *)
{
    const char *agent = CFG_OID_GET_INST_NAME(oid, 1);
    const char *iface = CFG_OID_GET_INST_NAME(oid, 2);
    te_errno rc;

    /*
     * Try to disable FW LLDP using private flag if it is present.
     * FW LLDP generates packets from Intel X710 which randomly break
     * tests because of unexpected packets observed.
     */
    rc = tapi_cfg_if_priv_flag_set(agent, iface, "disable-fw-lldp", true);
    if (rc != 0 && rc != TE_RC(TE_CS, TE_ENOENT))
    {
        TEST_VERDICT("Attempt to disable FW LLDP on TA %s interface %s using private flag failed unexpectedly: %r",
                     agent, iface, rc);
    }

    return 0;
}

/* Add TRC tag named after network interface driver */
static te_errno
add_driver_tag(const char *ta, const char *prefix)
{
    char *drv_name = NULL;
    te_string str = TE_STRING_INIT_STATIC(DEF_STR_LEN);
    te_errno rc;

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_NET, &drv_name);
    if (rc != 0)
        return rc;

    if (!te_str_is_null_or_empty(drv_name))
    {
        te_string_append(&str, "%s%s", prefix, drv_name);
        rc = tapi_tags_add_tag(te_string_value(&str), NULL);
    }

    free(drv_name);
    return rc;
}

static te_errno
get_phy_local_param(const char *ta, const char *param, char **value)
{
    te_errno rc;

    rc = cfg_get_instance_string_fmt(value, "/local:%s/phy:/%s:", ta, param);
    if (rc != 0)
    {
        if (rc != TE_RC(TE_CS, TE_ENOENT))
            return rc;

        *value = NULL;
    }

    return 0;
}

static void
add_local_phy_tags(const char *ta, const char *prefix)
{
    char *autoneg_str;
    char *duplex_str;
    char *speed_str;
    int autoneg;
    int duplex;
    int speed;

    CHECK_RC(get_phy_local_param(ta, "autoneg", &autoneg_str));
    CHECK_RC(get_phy_local_param(ta, "duplex", &duplex_str));
    CHECK_RC(get_phy_local_param(ta, "speed", &speed_str));

    autoneg = net_drv_ts_phy_autoneg_str2id(autoneg_str);
    duplex = net_drv_ts_phy_duplex_str2id(duplex_str);
    speed = net_drv_ts_phy_speed_str2id(speed_str);

    if (autoneg != TE_PHY_AUTONEG_UNKNOWN)
    {
        te_string str = TE_STRING_INIT_STATIC(DEF_STR_LEN);

        te_string_append(&str, "%sphy-autoneg-%s", prefix, autoneg_str);
        CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));
    }

    if (duplex != TE_PHY_DUPLEX_UNKNOWN)
    {
        te_string str = TE_STRING_INIT_STATIC(DEF_STR_LEN);

        te_string_append(&str, "%sphy-duplex-%s", prefix, duplex_str);
        CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));
    }

    if (speed != TE_PHY_SPEED_UNKNOWN)
    {
        te_string str = TE_STRING_INIT_STATIC(DEF_STR_LEN);

        te_string_append(&str, "%sphy-speed-%sMbps", prefix, speed_str);
        CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));
    }

    free(autoneg_str);
    free(duplex_str);
    free(speed_str);
}

static void
prepare_ipv6(void)
{
    te_errno rc;

    TEST_SUBSTEP("Try to enable IPv6 support.");
    rc = tapi_cfg_net_enable_ipv6_support();
    if (rc != 0)
    {
        int err = TE_RC_GET_ERROR(rc);

        if (err == TE_ENOENT || err == TE_ENOSYS || err == TE_EOPNOTSUPP)
        {
            WARN("All IPv6 tests will be skipped");
            RING_VERDICT("IPv6 is not supported");
            CHECK_RC(tapi_reqs_modify("!IP6"));
        }
        else
        {
            CHECK_RC(rc);
        }
    }
    else
    {
        TEST_SUBSTEP("Allocate and assign IPv6 addresses to be used by tests.");
        CHECK_RC(tapi_cfg_net_all_assign_ip(AF_INET6));
    }
}

int
main(int argc, char **argv)
{
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    te_bool env_init = FALSE;
    char *iut_drv_name = NULL;
    te_string str = TE_STRING_INIT;
    int combined_max;
    int rx_queues;

/* Redefine as empty to avoid environment processing here */
#undef TEST_START_VARS
#define TEST_START_VARS
#undef TEST_START_SPECIFIC
#define TEST_START_SPECIFIC
#undef TEST_END_SPECIFIC
#define TEST_END_SPECIFIC

    TEST_START_ENV_VARS;
    TEST_START;

    UNUSED(env);

    CHECK_RC(tapi_expand_path_all_ta(NULL));

    TEST_STEP("Remove empty networks from configuration.");
    if ((rc = tapi_cfg_net_remove_empty()) != 0)
        TEST_VERDICT("Failed to remove /net instances with empty interfaces");

    TEST_STEP("Reserve all resources specified in network configuration.");
    rc = tapi_cfg_net_reserve_all();
    if (rc != 0)
    {
        TEST_VERDICT("Failed to reserve all interfaces mentioned in networks "
                     "configuration: %r", rc);
    }

    TEST_STEP("Load (or reload) required kernel modules.");
    CHECK_RC(rcf_foreach_ta(load_required_modules, NULL));

    TEST_STEP("Bind net drivers to IUT and TST explicitly, because we can't "
              "rely on the driver to do this automatically.");
    rc = tapi_cfg_net_bind_driver_by_node(NET_NODE_TYPE_AGENT,
                                          NET_DRIVER_TYPE_NET);
    if (rc != 0)
        TEST_VERDICT("Failed to bind net driver on agent net node");

    rc = tapi_cfg_net_bind_driver_by_node(NET_NODE_TYPE_NUT,
                                          NET_DRIVER_TYPE_NET);
    if (rc != 0)
        TEST_VERDICT("Failed to bind net driver on nut net node");

    CFG_WAIT_CHANGES;
    CHECK_RC(rc = cfg_synchronize("/:", TRUE));

    TEST_STEP("Update network configuration to use interfaces instead of "
              "PCI functions.");
    CHECK_RC(tapi_cfg_net_nodes_update_pci_fn_to_interface(
                                              NET_NODE_TYPE_INVALID));

    TEST_STEP("Bring all used interfaces up.");
    CHECK_RC(tapi_cfg_net_all_up(FALSE));
    TEST_STEP("Delete previously assigned IPv4 addresses.");
    CHECK_RC(tapi_cfg_net_delete_all_ip4_addresses());
    TEST_STEP("Allocate and assign IPv4 addresses to be used by tests.");
    CHECK_RC(tapi_cfg_net_all_assign_ip(AF_INET));
    TEST_STEP("Prepare IPv6 configuration.");
    prepare_ipv6();

    TEST_STEP("Ensure that FW LLDP is disabled if applicable.");
    CHECK_RC(tapi_cfg_net_foreach_node(disable_fw_lldp, NULL));
    CFG_WAIT_CHANGES;

    TEST_STEP("Dump configuration to logs.");
    CHECK_RC(rc = cfg_synchronize("/:", TRUE));
    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    tapi_env_init(&env);
    env_init = TRUE;
    TEST_START_ENV;

    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);

    TEST_STEP("Bring all used interfaces PHY setup.");
    CHECK_RC(net_drv_set_phy_link(iut_rpcs->ta, iut_if->if_name));
    CHECK_RC(net_drv_set_phy_link(tst_rpcs->ta, tst_if->if_name));

    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    TEST_STEP("Collect and log TRC tags.");
    CHECK_RC(tapi_tags_add_linux_mm(iut_rpcs->ta, ""));
    CHECK_RC(add_driver_tag(iut_rpcs->ta, ""));
    CHECK_RC(add_driver_tag(tst_rpcs->ta, "peer-"));
    CHECK_RC(tapi_tags_add_firmwareversion_tag(iut_rpcs->ta,
                                               iut_if->if_name, ""));
    CHECK_RC(tapi_tags_add_net_pci_tags(iut_rpcs->ta, iut_if->if_name));
    CHECK_RC(tapi_tags_add_phy_tags(iut_rpcs->ta, iut_if->if_name,
                                    tst_rpcs->ta, tst_if->if_name, ""));
    add_local_phy_tags(iut_rpcs->ta, "iut-");
    add_local_phy_tags(tst_rpcs->ta, "tst-");

    CHECK_NOT_NULL(iut_drv_name = net_drv_driver_name(iut_rpcs->ta));
    if (!net_drv_driver_unloadable(iut_rpcs->ta, iut_drv_name))
        CHECK_RC(tapi_tags_add_tag("net-drv-shared", NULL));

    CHECK_RC(tapi_cfg_if_rss_rx_queues_get(iut_rpcs->ta, iut_if->if_name,
                                           &rx_queues));
    te_string_append(&str, "%d", rx_queues);
    CHECK_RC(tapi_tags_add_tag("rx-queues", te_string_value(&str)));

    CHECK_RC(tapi_cfg_if_chan_max_get(iut_rpcs->ta, iut_if->if_name,
                                      TAPI_CFG_IF_CHAN_COMBINED,
                                      &combined_max));
    te_string_reset(&str);
    te_string_append(&str, "%d", combined_max);
    CHECK_RC(tapi_tags_add_tag("max-combined-channels", te_string_value(&str)));

    TEST_SUCCESS;

cleanup:

    te_string_free(&str);
    free(iut_drv_name);

    if (env_init)
        TEST_END_ENV;

    TEST_END;
}
