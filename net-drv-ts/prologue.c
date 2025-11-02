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
#include "tapi_cfg_net.h"
#include "tapi_cfg_modules.h"
#include "tapi_cfg_pci.h"
#include "tapi_sh_env.h"
#include "tapi_cfg_pci.h"
#include "tapi_tags.h"

#define DEF_STR_LEN 1024

static te_errno
load_required_modules(const char *ta, void *cookie)
{
    te_errno rc;
    char *driver = NULL;
    char *net_driver = NULL;
    bool is_net_driver_needed = false;

    UNUSED(cookie);

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_NET, &driver);
    if (rc != 0)
        goto cleanup;

    if (driver != NULL)
    {
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
        rc = te_string_append(&str, "%s%s", prefix, drv_name);
        if (rc == 0)
            rc = tapi_tags_add_tag(te_string_value(&str), NULL);
    }

    free(drv_name);
    return rc;
}

int
main(int argc, char **argv)
{
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    te_bool env_init = FALSE;

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
    TEST_STEP("Allocate and assign IPv6 addresses to be used by tests.");
    CHECK_RC(tapi_cfg_net_all_assign_ip(AF_INET6));
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

    TEST_SUCCESS;

cleanup:

    if (env_init)
        TEST_END_ENV;

    TEST_END;
}
