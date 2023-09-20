/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Test Suite prologue
 *
 * Net Driver Test Suite prologue.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#ifndef DOXYGEN_TEST_SPEC

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
            rc = tapi_cfg_module_add_from_ta_dir(ta, "auxiliary", TRUE);
            if (rc == 0)
                rc = tapi_cfg_module_add_from_ta_dir(ta,
                                                     NET_DRV_X3_DRIVER_NAME,
                                                     TRUE);
        }
        else
        {
            rc = tapi_cfg_module_add(ta, driver, FALSE);
            if (rc != 0)
                goto cleanup;

            if (is_net_driver_needed)
            {
                rc = tapi_cfg_module_add(ta, net_driver, FALSE);
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

#define FUNC_CHECK_RC(_expr) \
    do {                            \
        rc = (_expr);               \
                                    \
        if (rc != 0)                \
            goto finish;            \
    } while (0)

/* Add TRC tags related to tested NIC */
static te_errno
add_nic_tags(const char *ta,
             const struct if_nameindex *intf)
{
    char *pci_oid = NULL;
    unsigned int vendor_id = 0;
    unsigned int device_id = 0;
    unsigned int sub_vendor_id = 0;
    unsigned int sub_device_id = 0;
    te_string str = TE_STRING_INIT_STATIC(DEF_STR_LEN);
    te_errno rc;

    rc = tapi_cfg_pci_oid_by_net_if(ta, intf->if_name, &pci_oid);
    if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        return 0;
    else if (rc != 0)
        return rc;

    FUNC_CHECK_RC(tapi_cfg_pci_get_vendor_dev_ids(
                        pci_oid, &vendor_id, &device_id,
                        &sub_vendor_id,
                        &sub_device_id));

    FUNC_CHECK_RC(te_string_append(&str, "pci-%04x", vendor_id));
    FUNC_CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));

    FUNC_CHECK_RC(te_string_append(&str, "-%04x", device_id));
    FUNC_CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));

    te_string_reset(&str);
    FUNC_CHECK_RC(te_string_append(&str, "pci-sub-%04x", sub_vendor_id));
    FUNC_CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));

    FUNC_CHECK_RC(te_string_append(&str, "-%04x", sub_device_id));
    FUNC_CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));

finish:

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

    if ((rc = tapi_cfg_net_remove_empty()) != 0)
        TEST_VERDICT("Failed to remove /net instances with empty interfaces");

    rc = tapi_cfg_net_reserve_all();
    if (rc != 0)
    {
        TEST_VERDICT("Failed to reserve all interfaces mentioned in networks "
                     "configuration: %r", rc);
    }

    CHECK_RC(rcf_foreach_ta(load_required_modules, NULL));

    /*
     * Bind net drivers to IUT and TST explicitly, because we can't
     * rely on the driver to do this automatically.
     */
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

    CHECK_RC(tapi_cfg_net_nodes_update_pci_fn_to_interface(
                                              NET_NODE_TYPE_INVALID));

    CHECK_RC(tapi_cfg_net_all_up(FALSE));
    CHECK_RC(tapi_cfg_net_delete_all_ip4_addresses());
    CHECK_RC(tapi_cfg_net_all_assign_ip(AF_INET));
    CHECK_RC(tapi_cfg_net_all_assign_ip(AF_INET6));
    CFG_WAIT_CHANGES;

    CHECK_RC(rc = cfg_synchronize("/:", TRUE));
    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    tapi_env_init(&env);
    env_init = TRUE;
    TEST_START_ENV;

    TEST_GET_IF(iut_if);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);

    CHECK_RC(add_driver_tag(iut_rpcs->ta, ""));
    CHECK_RC(add_driver_tag(tst_rpcs->ta, "peer-"));
    CHECK_RC(add_nic_tags(iut_rpcs->ta, iut_if));

    TEST_SUCCESS;

cleanup:

    if (env_init)
        TEST_END_ENV;

    TEST_END;
}

#endif /* !DOXYGEN_TEST_SPEC */
