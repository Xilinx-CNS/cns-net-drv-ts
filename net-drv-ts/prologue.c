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
#include "tapi_cfg_cpu.h"
#include "tapi_sh_env.h"
#include "tapi_rpc_unistd.h"
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
disable_fw_lldp(cfg_net_t *net, cfg_net_node_t *node, const char *str,
                cfg_oid *oid, void *cookie)
{
    const char *agent = CFG_OID_GET_INST_NAME(oid, 1);
    const char *iface = CFG_OID_GET_INST_NAME(oid, 2);
    te_errno rc;

    UNUSED(net);
    UNUSED(node);
    UNUSED(str);
    UNUSED(cookie);

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

static te_errno
add_tun_support_tag(rcf_rpc_server *rpcs, const char *prefix)
{
    te_errno rc;
    te_string tag = TE_STRING_INIT_STATIC(DEF_STR_LEN);
    int fd;

    RPC_AWAIT_ERROR(rpcs);
    fd = rpc_open(rpcs, "/dev/net/tun", RPC_O_RDWR, 0);
    if (fd >= 0)
    {
        RPC_CLOSE(rpcs, fd);
        return 0;
    }

    rc = RPC_ERRNO(rpcs);
    if (rc == RPC_ENOENT || rc == RPC_ENODEV || rc == RPC_EOPNOTSUPP)
    {
        INFO("TUN/TAP is not available on TA %s", rpcs->ta);
        te_string_append(&tag, "%sno-tun", prefix);
        rc = tapi_tags_add_tag(te_string_value(&tag), NULL);
        if (rc != 0)
            return rc;

        return 0;
    }

    ERROR("Failed to check TUN/TAP support on TA %s: %r", rpcs->ta, rc);
    return rc;
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

static void
add_cpus_tag(const char *ta, const char *prefix)
{
    te_string tag_name = TE_STRING_INIT;
    te_string tag_val = TE_STRING_INIT;
    size_t cpus;

    te_string_reset(&tag_name);
    te_string_append(&tag_name, "%s-cpus", prefix);

    CHECK_RC(tapi_cfg_get_all_threads(ta, &cpus, NULL));
    te_string_reset(&tag_val);
    te_string_append(&tag_val, "%zu", cpus);
    CHECK_RC(tapi_tags_add_tag(te_string_value(&tag_name),
                               te_string_value(&tag_val)));

    te_string_free(&tag_name);
    te_string_free(&tag_val);
}

static unsigned int
pci_device_pf_count_by_driver(const char *ta, const char *if_pci_oid,
                              unsigned int domain, unsigned int bus,
                              unsigned int device)
{
    unsigned int drv_dev_num = 0;
    char **drv_pci_oids = NULL;
    unsigned int pf_count = 0;
    char *net_driver = NULL;
    unsigned int i;
    te_errno rc;

    rc = tapi_cfg_pci_get_driver(if_pci_oid, &net_driver);
    if (rc != 0)
    {
        WARN("Failed to get PCI driver for '%s': %r", if_pci_oid, rc);
        return 0;
    }

    if (te_str_is_null_or_empty(net_driver))
    {
        WARN("Empty PCI driver for '%s'", if_pci_oid);
        free(net_driver);
        return 0;
    }

    rc = tapi_cfg_pci_devices_by_driver(ta, net_driver, &drv_dev_num,
                                        &drv_pci_oids);
    if (rc != 0)
    {
        WARN("Failed to get PCI devices for driver '%s' on TA %s: %r",
             net_driver, ta, rc);
        free(net_driver);
        return 0;
    }

    for (i = 0; i < drv_dev_num; i++)
    {
        unsigned int drv_domain;
        unsigned int drv_bus;
        unsigned int drv_device;
        bool is_pf = false;

        rc = tapi_cfg_pci_get_dbdf(drv_pci_oids[i], &drv_domain, &drv_bus,
                                   &drv_device, NULL);
        if (rc != 0)
            continue;

        if (drv_domain != domain || drv_bus != bus || drv_device != device)
            continue;

        rc = tapi_cfg_pci_is_pf(drv_pci_oids[i], &is_pf);
        if (rc != 0 || !is_pf)
            continue;

        pf_count++;
    }

    for (i = 0; i < drv_dev_num; i++)
        free(drv_pci_oids[i]);
    free(drv_pci_oids);
    free(net_driver);

    return pf_count;
}

static te_errno
pci_port_is_active(const char *ta, const char *if_name, te_bool *is_active)
{
    te_errno rc;

    *is_active = false;

    rc = tapi_cfg_base_if_await_link_up(ta, if_name, 0, 0, 0);
    if (rc == 0)
    {
        *is_active = true;
        return 0;
    }

    if (TE_RC_GET_ERROR(rc) == TE_ETIMEDOUT)
        return 0;

    return rc;
}

static unsigned int
pci_pf_active_port_count(const char *ta, const char *pci_oid)
{
    cfg_handle *port_handles = NULL;
    unsigned int active_port_count = 0;
    unsigned int port_num = 0;
    unsigned int i;
    te_errno rc;

    rc = cfg_find_pattern_fmt(&port_num, &port_handles, "%s/net:*", pci_oid);
    if (rc != 0 || port_num == 0)
        goto out;

    for (i = 0; i < port_num; i++)
    {
        char *port_if_name = NULL;
        bool is_active = false;

        rc = cfg_get_instance(port_handles[i], NULL, &port_if_name);
        if (rc != 0)
        {
            free(port_if_name);
            continue;
        }

        rc = pci_port_is_active(ta, port_if_name, &is_active);
        if (rc == 0 && is_active)
            active_port_count++;

        free(port_if_name);
    }

out:
    free(port_handles);
    return active_port_count;
}

static unsigned int
pci_device_active_port_count(const char *ta, unsigned int domain,
                             unsigned int bus, unsigned int device)
{
    cfg_handle *dev_handles = NULL;
    unsigned int active_port_count = 0;
    unsigned int dev_num = 0;
    unsigned int i;
    te_errno rc;

    rc = cfg_find_pattern_fmt(&dev_num, &dev_handles,
                              "/agent:%s/hardware:/pci:/device:*", ta);
    if (rc != 0)
    {
        WARN("Failed to list PCI devices on TA %s: %r", ta, rc);
        return 0;
    }

    for (i = 0; i < dev_num; i++)
    {
        char *dev_oid = NULL;
        unsigned int dev_domain;
        unsigned int dev_bus;
        unsigned int dev_device;
        bool is_pf = false;

        rc = cfg_get_oid_str(dev_handles[i], &dev_oid);
        if (rc != 0)
            continue;

        rc = tapi_cfg_pci_get_dbdf(dev_oid, &dev_domain, &dev_bus,
                                   &dev_device, NULL);
        if (rc != 0 || dev_domain != domain || dev_bus != bus ||
            dev_device != device)
        {
            free(dev_oid);
            continue;
        }

        rc = tapi_cfg_pci_is_pf(dev_oid, &is_pf);
        if (rc != 0 || !is_pf)
        {
            free(dev_oid);
            continue;
        }

        active_port_count += pci_pf_active_port_count(ta, dev_oid);
        free(dev_oid);
    }

    free(dev_handles);
    return active_port_count;
}

static void
add_pci_device_count_tags(const char *ta, const char *if_name)
{
    te_string active_ports_tag_value = TE_STRING_INIT;
    te_string pf_tag_value = TE_STRING_INIT;
    unsigned int active_port_count;
    char *if_pci_oid = NULL;
    unsigned int pf_count;
    unsigned int domain;
    unsigned int device;
    unsigned int bus;
    te_errno rc;

    rc = tapi_cfg_pci_oid_by_net_if(ta, if_name, &if_pci_oid);
    if (rc != 0)
    {
        WARN("Failed to get PCI OID for interface %s on TA %s: %r",
             if_name, ta, rc);
        goto out;
    }

    rc = tapi_cfg_pci_get_dbdf(if_pci_oid, &domain, &bus, &device, NULL);
    if (rc != 0)
    {
        WARN("Failed to parse PCI OID '%s': %r", if_pci_oid, rc);
        goto out;
    }

    pf_count = pci_device_pf_count_by_driver(ta, if_pci_oid,
                                             domain, bus, device);
    active_port_count = pci_device_active_port_count(ta, domain, bus, device);

    if (pf_count > 0)
    {
        te_string_append(&pf_tag_value, "%u", pf_count);
        CHECK_RC(tapi_tags_add_tag("pf-count",
                 te_string_value(&pf_tag_value)));
    }

    if (active_port_count > 0)
    {
        te_string_append(&active_ports_tag_value, "%u", active_port_count);
        CHECK_RC(tapi_tags_add_tag("active-port-count",
                                   te_string_value(&active_ports_tag_value)));
    }

out:
    free(if_pci_oid);
    te_string_free(&pf_tag_value);
    te_string_free(&active_ports_tag_value);
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

    TEST_STEP("Wait until link status becomes UP on both IUT and Tester.");
    net_drv_wait_up_gen(iut_rpcs->ta, iut_if->if_name, false);
    net_drv_wait_up_gen(tst_rpcs->ta, tst_if->if_name, false);

    CHECK_RC(rc = cfg_synchronize("/:", TRUE));
    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    TEST_STEP("Collect and log TRC tags.");
    CHECK_RC(tapi_tags_add_linux_mm(iut_rpcs->ta, ""));
    CHECK_RC(add_driver_tag(iut_rpcs->ta, ""));
    CHECK_RC(add_driver_tag(tst_rpcs->ta, "peer-"));
    CHECK_RC(add_tun_support_tag(iut_rpcs, "iut-"));
    CHECK_RC(add_tun_support_tag(tst_rpcs, "tst-"));
    CHECK_RC(net_drv_add_missing_ethtool_opt_tags(iut_rpcs));
    if (!net_drv_is_ptp_supported(iut_rpcs, iut_if->if_name))
        CHECK_RC(tapi_tags_add_tag("iut-no-ptp", NULL));
    if (!net_drv_is_ptp_supported(tst_rpcs, tst_if->if_name))
        CHECK_RC(tapi_tags_add_tag("tst-no-ptp", NULL));
    CHECK_RC(tapi_tags_add_firmwareversion_tag(iut_rpcs->ta,
                                               iut_if->if_name, ""));
    CHECK_RC(tapi_tags_add_net_pci_tags(iut_rpcs->ta, iut_if->if_name));
    add_pci_device_count_tags(iut_rpcs->ta, iut_if->if_name);
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

    add_cpus_tag(iut_rpcs->ta, "iut");
    add_cpus_tag(tst_rpcs->ta, "tst");

    TEST_SUCCESS;

cleanup:

    te_string_free(&str);
    free(iut_drv_name);

    if (env_init)
        TEST_END_ENV;

    TEST_END;
}
