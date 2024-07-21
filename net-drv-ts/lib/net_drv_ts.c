/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Common test API
 *
 * Implementation of common test API.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

/** Log user for this file */
#define TE_LGR_USER "Library"

#include "net_drv_ts.h"
#include "tapi_cfg.h"
#include "tapi_cfg_if.h"
#include "tapi_cfg_pci.h"
#include "tapi_cfg_modules.h"
#include "tapi_cfg_base.h"
#include "te_ethernet.h"
#include "tapi_cfg_phy.h"

#define MAX_PKT_LEN 1024

/* See description in net_drv_ts.h */
char *
net_drv_driver_name(const char *ta)
{
    char *name = NULL;
    te_errno rc;

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_NET, &name);
    if (rc != 0)
    {
        ERROR("Failed to get network driver name for agent %s", ta);
        return NULL;
    }

    /*
     * In the case of Virtio, PCI device is bound to virtio-pci driver,
     * but network device is provided by the virtio_net driver which
     * is provided by the module with exactly the same name.
     */
    if (strcmp(name, "virtio-pci") == 0)
    {
        free(name);
        name = strdup("virtio_net");
        CHECK_NOT_NULL(name);
    }

    return name;
}

/* See description in net_drv_ts.h */
te_bool
net_drv_driver_unloadable(const char *ta, const char *module)
{
    te_errno rc;
    te_bool all_grabbed = FALSE;

    rc = tapi_cfg_module_check_devices(ta, module, &all_grabbed);
    if (rc == 0 && all_grabbed)
        return TRUE;

    return FALSE;
}

/* See description in net_drv_ts.h */
te_errno
net_drv_driver_set_loaded(const char *ta,
                          const char *module,
                          te_bool load)
{
    te_errno rc;
    int loaded;
    cfg_val_type cvt_int = CVT_INTEGER;

    if (load)
    {
        rc = tapi_cfg_module_load(ta, module);
    }
    else
    {
        rc = cfg_set_instance_fmt(CFG_VAL(INTEGER, 1),
                                  "/agent:%s/module:%s/unload_holders:",
                                  ta, module);
        if (rc != 0)
        {
            ERROR("Failed to set unload holders for the module '%s' on %s: %r",
                  module, ta, rc);
            return rc;
        }

        rc = tapi_cfg_module_unload(ta, module);
    }

    if (rc != 0)
        return rc;

    rc = cfg_get_instance_fmt(&cvt_int, &loaded,
                              "/agent:%s/module:%s/loaded:",
                              ta, module);
    if (rc != 0)
    {
        ERROR("%s(): failed to get module loaded state, rc=%r",
              __FUNCTION__, rc);
        return rc;
    }

    if (load != (loaded != 0))
    {
        ERROR("%s(): module loaded state has unexpected value",
              __FUNCTION__);
        return TE_EFAIL;
    }

    return 0;
}

/* See description in net_drv_ts.h */
void
net_drv_req_if_feature_change(const char *ta, const char *if_name,
                              const char *feature_name)
{
    te_bool present;
    te_bool readonly;
    int status;

    CHECK_RC(tapi_cfg_if_feature_is_present(ta, if_name, feature_name,
                                            &present));
    if (!present)
    {
        ERROR("Feature '%s' is not present for interface '%s' on "
              "agent '%s'", feature_name, if_name, ta);

        TEST_SKIP("Feature '%s' is not supported", feature_name);
    }

    CHECK_RC(tapi_cfg_if_feature_is_readonly(ta, if_name, feature_name,
                                             &readonly));
    if (readonly)
    {
        CHECK_RC(tapi_cfg_if_feature_get(ta, if_name, feature_name,
                                         &status));
        ERROR("Feature '%s' is %s and read-only for interface '%s' on "
              "agent '%s'", feature_name,
              (status == 0 ? "disabled" : "enabled"), if_name, ta);

        TEST_SKIP("Feature '%s' cannot be modified", feature_name);
    }
}

/**
 * Try to set a given interface feature (/interface:/feature: node).
 *
 * @param ta                  Test Agent name
 * @param if_name             Interface name
 * @param feature_name        Feature name
 * @param req_status          Requested status: @c 0 (disabled) or
 *                            @c 1 (enabled)
 * @param fine_if_impossible  If @c TRUE, simply print warning if feature
 *                            cannot be set to the requested status;
 *                            otherwise skip the test in such case
 */
void
net_drv_set_if_feature_gen(const char *ta, const char *if_name,
                           const char *feature_name, int req_status,
                           te_bool fine_if_impossible)
{
    te_bool present;
    te_bool readonly;
    const char *req_status_str = (req_status == 0 ? "disabled" : "enabled");
    int status;

    CHECK_RC(tapi_cfg_if_feature_is_present(ta, if_name, feature_name,
                                            &present));
    if (!present)
    {
        if (req_status == 0 || fine_if_impossible)
        {
            WARN("Feature '%s' is not present for interface '%s' on agent "
                 "'%s', consider it to be disabled", feature_name, if_name,
                 ta);
            return;
        }
        else
        {
            WARN("Feature '%s' is not present for interface '%s' on agent "
                 "'%s'", feature_name, if_name, ta);

            TEST_SKIP("Feature '%s' is not supported", feature_name);
        }
    }

    CHECK_RC(tapi_cfg_if_feature_get(ta, if_name, feature_name, &status));
    if (status == req_status)
    {
        RING("Feature '%s' is already %s for interface '%s' on "
             "agent '%s'", feature_name, req_status_str,
             if_name, ta);
        return;
    }

    CHECK_RC(tapi_cfg_if_feature_is_readonly(ta, if_name, feature_name,
                                             &readonly));
    if (readonly)
    {
        WARN("Feature '%s' is read-only for interface '%s' on "
             "agent '%s' and therefore cannot be %s",
             feature_name, if_name, ta, req_status_str);

        if (fine_if_impossible)
            return;

        TEST_SKIP("Feature '%s' is %s and cannot be modified",
                  feature_name, (status == 0 ? "disabled" : "enabled"));
    }

    CHECK_RC(tapi_cfg_if_feature_set(ta, if_name, feature_name,
                                     req_status));
}

/* See description in net_drv_ts.h */
void
net_drv_set_if_feature(const char *ta, const char *if_name,
                       const char *feature_name, int req_status)
{
    net_drv_set_if_feature_gen(ta, if_name, feature_name, req_status,
                               FALSE);
}

/* See description in net_drv_ts.h */
void
net_drv_try_set_if_feature(const char *ta, const char *if_name,
                           const char *feature_name, int req_status)
{
    net_drv_set_if_feature_gen(ta, if_name, feature_name, req_status,
                               TRUE);
}

/* See description in net_drv_ts.h */
size_t
net_drv_send_recv_check(rcf_rpc_server *rpcs_sender,
                        int s_sender,
                        rcf_rpc_server *rpcs_receiver,
                        int s_receiver,
                        const char *vpref)
{
    return net_drv_sendto_recv_check(rpcs_sender, s_sender, NULL,
                                     rpcs_receiver, s_receiver, vpref);
}

static size_t
net_drv_sendto_recv_check_gen(rcf_rpc_server *rpcs_sender,
                              int s_sender,
                              const struct sockaddr *dst_addr,
                              rcf_rpc_server *rpcs_receiver,
                              int s_receiver,
                              const char *vpref, te_bool may_loss)
{
    char send_buf[MAX_PKT_LEN];
    char recv_buf[MAX_PKT_LEN];
    te_bool readable;
    int len;
    int rc;
    int total_len = 0;

    if (vpref == NULL || *vpref == '\0')
        vpref = "Data transmission check";

    len = rand_range(1, MAX_PKT_LEN);
    te_fill_buf(send_buf, len);

    RPC_AWAIT_ERROR(rpcs_sender);
    if (dst_addr != NULL)
    {
        rc = rpc_sendto(rpcs_sender, s_sender, send_buf, len, 0,
                        dst_addr);
    }
    else
    {
        rc = rpc_send(rpcs_sender, s_sender, send_buf, len, 0);
    }
    if (rc < 0)
    {
        TEST_VERDICT("%s: sending failed with error " RPC_ERROR_FMT,
                     vpref, RPC_ERROR_ARGS(rpcs_sender));
    }
    else if (rc != len)
    {
        TEST_VERDICT("%s: sending returned unexpected result", vpref);
    }

    RPC_GET_READABILITY(readable, rpcs_receiver, s_receiver,
                        TAPI_WAIT_NETWORK_DELAY);
    if (!readable)
    {
        if (may_loss)
            return 0;

        TEST_VERDICT("%s: receiver did not become readable", vpref);
    }

    while (readable)
    {
        RPC_AWAIT_ERROR(rpcs_receiver);
        rc = rpc_recv(rpcs_receiver, s_receiver, &recv_buf[total_len],
                      sizeof(recv_buf) - total_len, 0);
        if (rc < 0)
        {
            TEST_VERDICT("%s: recv() failed with error " RPC_ERROR_FMT,
                         vpref, RPC_ERROR_ARGS(rpcs_receiver));
        }
        total_len += rc;
        if (total_len >= len)
            break;
        RPC_GET_READABILITY(readable, rpcs_receiver, s_receiver,
                            TAPI_WAIT_NETWORK_DELAY);
    }
    if (total_len != len)
    {
        TEST_VERDICT("%s: recv() returned unexpected result", vpref);
    }
    else if (memcmp(send_buf, recv_buf, len) != 0)
    {
        TEST_VERDICT("%s: recv() returned unexpected data", vpref);
    }

    return len;
}

/* See description in net_drv_ts.h */
size_t
net_drv_sendto_recv_check(rcf_rpc_server *rpcs_sender,
                          int s_sender,
                          const struct sockaddr *dst_addr,
                          rcf_rpc_server *rpcs_receiver,
                          int s_receiver,
                          const char *vpref)
{
    return net_drv_sendto_recv_check_gen(rpcs_sender, s_sender, dst_addr,
                                         rpcs_receiver, s_receiver, vpref,
                                         FALSE);
}

/* See description in net_drv_ts.h */
size_t
net_drv_sendto_recv_check_may_loss(rcf_rpc_server *rpcs_sender,
                                   int s_sender,
                                   const struct sockaddr *dst_addr,
                                   rcf_rpc_server *rpcs_receiver,
                                   int s_receiver,
                                   const char *vpref)
{
    return net_drv_sendto_recv_check_gen(rpcs_sender, s_sender, dst_addr,
                                         rpcs_receiver, s_receiver, vpref,
                                         TRUE);
}

/* See description in net_drv_ts.h */
void
net_drv_conn_check(rcf_rpc_server *rpcs1,
                   int s1,
                   const char *s1_name,
                   rcf_rpc_server *rpcs2,
                   int s2,
                   const char *s2_name,
                   const char *vpref)
{
    te_string pref_str = TE_STRING_INIT;

    CHECK_RC(te_string_append(&pref_str, "%s, sending data from %s to %s",
                              vpref, s1_name, s2_name));
    net_drv_send_recv_check(rpcs1, s1, rpcs2, s2, pref_str.ptr);

    te_string_reset(&pref_str);
    CHECK_RC(te_string_append(&pref_str, "%s, sending data from %s to %s",
                              vpref, s2_name, s1_name));
    net_drv_send_recv_check(rpcs2, s2, rpcs1, s1, pref_str.ptr);

    te_string_free(&pref_str);
}

/* See description in net_drv_ts.h */
te_errno
net_drv_cat_all_files(rcf_rpc_server *rpcs, uint32_t timeout,
                      const char *path_fmt, ...)
{
#define FIND_FMT "find '%s' -perm /a+r ! -type d ! -type l ! -name \".*\""

    va_list ap;
    te_errno rc;
    rpc_wait_status status;

    te_string path = TE_STRING_INIT;

    va_start(ap, path_fmt);
    rc = te_string_append_va(&path, path_fmt, ap);
    va_end(ap);

    if (rc != 0)
        return TE_RC(TE_TAPI, rc);

    RPC_AWAIT_ERROR(rpcs);
    status = rpc_system_ex(rpcs, FIND_FMT " | grep . >/dev/null", path.ptr);
    if (status.flag != RPC_WAIT_STATUS_EXITED ||
        status.value != 0)
    {
        ERROR("%s(): failed to enumerate any matching files in %s",
              __FUNCTION__, path.ptr);
        te_string_free(&path);
        return TE_RC(TE_TAPI, TE_ENOENT);
    }

    RPC_AWAIT_ERROR(rpcs);
    if (timeout != 0)
        rpcs->timeout = timeout;
    status = rpc_system_ex(rpcs,
                           FIND_FMT " | xargs -I {} "
                           "bash -c \"echo cat {}; cat {}\"",
                           path.ptr);

    te_string_free(&path);

    if (status.flag != RPC_WAIT_STATUS_EXITED ||
        status.value != 0)
    {
        ERROR("%s(): a command finished with unexpected exit status",
              __FUNCTION__);
        return TE_RC(TE_TAPI, TE_EFAIL);
    }

    return 0;

#undef FIND_FMT
}

/* See description in net_drv_ts.h */
te_errno
net_drv_set_check_mac(const char *ta, const char *if_name,
                      const void *mac_addr)
{
    uint8_t got_addr[ETHER_ADDR_LEN] = { 0, };
    size_t addr_len;
    te_errno rc;

    rc = tapi_cfg_set_hwaddr(ta, if_name, mac_addr,
                             ETHER_ADDR_LEN);
    if (rc != 0)
        return rc;

    CFG_WAIT_CHANGES;

    rc = cfg_synchronize_fmt(TRUE, "/agent:%s/interface:%s",
                             ta, if_name);
    if (rc != 0)
        return rc;

    addr_len = sizeof(got_addr);
    rc = tapi_cfg_get_hwaddr(ta, if_name, got_addr, &addr_len);
    if (rc != 0)
        return rc;

    if (addr_len != ETHER_ADDR_LEN ||
        memcmp(mac_addr, got_addr, addr_len) != 0)
    {
        ERROR("%s(): after setting MAC address to " TE_PRINTF_MAC_FMT
              " the new address is reported to be " TE_PRINTF_MAC_FMT
              " instead", __FUNCTION__,
              TE_PRINTF_MAC_VAL((const uint8_t *)mac_addr),
              TE_PRINTF_MAC_VAL(got_addr));

        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    return 0;
}

/* See description in net_drv_ts.h */
void
net_drv_set_mtu(const char *ta, const char *if_name, int mtu,
                const char *where)
{
    te_errno rc;

    rc = tapi_cfg_base_if_set_mtu(ta, if_name, mtu, NULL);
    if (rc != 0)
        TEST_VERDICT("Failed to set MTU on %s, rc=%r", where, rc);
}

/* See description in net_drv_ts.h */
te_errno
net_drv_neigh_nodes_count(const char *ta, unsigned int *num)
{
    te_errno rc;
    cfg_handle *handles;

    rc = cfg_find_pattern_fmt(num, &handles,
                              "/agent:%s/sys:/net:/ipv*:/neigh:*",
                              ta);
    if (rc == TE_RC(TE_CS, TE_ENOENT))
    {
        rc = 0;
        *num = 0;
    }
    else if (rc == 0)
    {
        free(handles);
    }

    return rc;
}

/* See description in net_drv_ts.h */
te_errno
net_drv_wait_neigh_nodes_recover(const char *ta, unsigned int num)
{
    const int attempts = 100;
    const int sleep_time = 1000;
    int i;
    te_errno rc;

    unsigned int cur_num;

    for (i = 0; i < attempts; i++)
    {
        rc = cfg_synchronize_fmt(TRUE, "/agent:%s", ta);
        if (rc != 0)
            return rc;

        rc = net_drv_neigh_nodes_count(ta, &cur_num);
        if (rc != 0)
            return rc;
        if (cur_num >= num)
            return 0;

        if (i < attempts - 1)
        {
            te_motivated_msleep(sleep_time, "wait until neigh nodes "
                                "appear in configuration tree");
        }
    }

    ERROR("Failed to wait until all neigh nodes on agent %s are available",
          ta);
    return TE_RC(TE_TAPI, TE_ENOENT);
}

void
net_drv_set_pci_param_uint(const char *pci_oid,
                           const char *param_name,
                           tapi_cfg_pci_param_cmode cmode,
                           uint64_t value,
                           const char *vpref)
{
    te_errno rc;
    uint64_t new_value;

    rc = tapi_cfg_pci_set_param_uint(pci_oid, param_name, cmode, value);
    if (rc != 0)
    {
        TEST_VERDICT("%s: failed to set parameter %s, rc = %r",
                     vpref, param_name, rc);
    }

    CHECK_RC(cfg_synchronize(pci_oid, TRUE));

    rc = tapi_cfg_pci_get_param_uint(pci_oid, param_name, cmode,
                                     &new_value);
    if (rc != 0)
    {
        TEST_VERDICT("%s: failed to get parameter %s after setting it, "
                     "rc = %r", vpref, param_name, rc);
    }

    if (new_value != value)
    {
        ERROR("Parameter %s was set to %" TE_PRINTF_64 "u but is "
              "%" TE_PRINTF_64 "u instead", param_name, value, new_value);
        TEST_VERDICT("%s: parameter %s has unexpected value after "
                     "setting it", vpref, param_name);
    }
}

/* See description in net_drv_ts.h */
void
net_drv_wait_up_gen(const char *ta, const char *if_name, te_bool cleanup)
{
/*
 * Maximum timeout in second for waiting for interface is UP. For more info
 * see SWNETLINUX-4827.
 */
#define MAX_IF_WAIT_MS 160000

    te_errno rc;

    rc = tapi_cfg_phy_state_wait_up(ta, if_name, MAX_IF_WAIT_MS);
    if (rc != 0)
    {
        if (cleanup)
        {
            ERROR_VERDICT("Failed to wait until interface becomes UP, "
                          "rc=%r", rc);
        }
        else
        {
            TEST_VERDICT("Failed to wait until interface becomes UP, rc=%r",
                         rc);
        }
    }
#undef MAX_IF_WAIT_MS
}

/* See description in net_drv_ts.h */
void
net_drv_ts_add_vlan(const char *ta1, const char *ta2,
                    const char *if1, const char *if2,
                    int af, uint16_t *vlan_id,
                    struct sockaddr **vlan_addr1,
                    struct sockaddr **vlan_addr2)
{
    uint16_t new_vlan_id;
    char *vlan_if1 = NULL;
    char *vlan_if2 = NULL;
    struct sockaddr *new_addr1 = NULL;
    struct sockaddr *new_addr2 = NULL;
    int prefix;

    if (vlan_id != NULL && *vlan_id != 0)
        new_vlan_id = *vlan_id;
    else
        new_vlan_id = rand_range(1, 4094);

    CHECK_RC(tapi_cfg_base_if_add_vlan(ta1, if1,
                                       new_vlan_id, &vlan_if1));
    CHECK_RC(tapi_cfg_base_if_add_vlan(ta2, if2,
                                       new_vlan_id, &vlan_if2));
    CHECK_RC(tapi_cfg_base_if_up(ta1, vlan_if1));
    CHECK_RC(tapi_cfg_base_if_up(ta2, vlan_if2));
    CHECK_RC(tapi_cfg_alloc_af_net_addr_pair(af, &new_addr1, &new_addr2,
                                             &prefix));
    CHECK_RC(tapi_cfg_base_if_add_net_addr(ta1, vlan_if1,
                                           new_addr1, prefix, true, NULL));
    CHECK_RC(tapi_cfg_base_if_add_net_addr(ta2, vlan_if2,
                                           new_addr2, prefix, true, NULL));

    free(vlan_if1);
    free(vlan_if2);

    if (vlan_id != NULL)
        *vlan_id = new_vlan_id;

    if (vlan_addr1 != NULL)
        *vlan_addr1 = new_addr1;
    else
        free(new_addr1);

    if (vlan_addr2 != NULL)
        *vlan_addr2 = new_addr2;
    else
        free(new_addr2);
}
