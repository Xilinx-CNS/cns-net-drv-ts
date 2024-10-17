/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Common test API
 *
 * Declarations of common test API.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#ifndef __TS_NET_DRV_TS_H__
#define __TS_NET_DRV_TS_H__

#include "te_config.h"

#include "tapi_test.h"
#include "tapi_env.h"
#include "tapi_rpcsock_macros.h"
#include "te_sleep.h"
#include "tapi_cfg_pci.h"

/* X3 driver name */
#define NET_DRV_X3_DRIVER_NAME "xilinx_efct"

#define NET_DRV_IONIC_DRIVER_NAME "ionic"

/**
 * Wait until interface statistics are updated after sending or
 * receiving something. Increase sleep time from 1 to 2 according
 * to X3-1135.
 */
#define NET_DRV_WAIT_IF_STATS_UPDATE \
    te_motivated_sleep(2, "wait until interface statistics are updated")

/**
 * Get tested driver name on TA.
 *
 * @param ta      Test Agent name.
 *
 * @return Pointer to dynamically allocated string, it should be
 *         released by caller. On failure @c NULL is returned.
 */
extern char *net_drv_driver_name(const char *ta);

/**
 * Is driver unloadable?
 *
 * @return @c TRUE or @c FALSE.
 */
extern te_bool net_drv_driver_unloadable(const char *ta,
                                         const char *module);

/**
 * Load or unload driver module, check that it succeeded.
 *
 * @param ta      Test Agent name.
 * @param module  Kernel module name.
 * @param load    If @c TRUE, load the module, otherwise unload it.
 *
 * @return Status code.
 */
extern te_errno net_drv_driver_set_loaded(const char *ta,
                                          const char *module,
                                          te_bool load);

/**
 * Check whether a given interface feature (/interface:/feature: node)
 * is present and can be modified for a given network interface.
 *
 * @param ta            Test Agent name
 * @param if_name       Interface name
 * @param feature_name  Feature name
 *
 * @return True if the feature is present and not read-only.
 */
extern bool net_drv_req_if_feature_configurable(const char *ta,
                                                const char *if_name,
                                                const char *feature_name);

/**
 * Check whether a given interface feature (/interface:/feature: node)
 * is present and can be modified for a given network interface.
 * If it is not true, skip the test with TEST_SKIP().
 *
 * @param ta            Test Agent name
 * @param if_name       Interface name
 * @param feature_name  Feature name
 */
extern void net_drv_req_if_feature_change(const char *ta,
                                          const char *if_name,
                                          const char *feature_name);

/**
 * Ensure that a given interface feature (/interface:/feature: node)
 * has the requested status, change it if necessary. Absence of the
 * feature is considered to be the same as it being disabled.
 * If the requested feature status cannot be achieved, skip the test
 * with TEST_SKIP().
 *
 * @param ta            Test Agent name
 * @param if_name       Interface name
 * @param feature_name  Feature name
 * @param req_status    Requested status: @c 0 (disabled) or @c 1 (enabled)
 */
extern void net_drv_set_if_feature(const char *ta,
                                   const char *if_name,
                                   const char *feature_name,
                                   int req_status);

/**
 * Try to set a given interface feature (/interface:/feature: node).
 * If the feature is not present or is read-only and has a wrong status,
 * print warning but do not fail the test.
 *
 * @param ta            Test Agent name
 * @param if_name       Interface name
 * @param feature_name  Feature name
 * @param req_status    Requested status: @c 0 (disabled) or @c 1 (enabled)
 */
extern void net_drv_try_set_if_feature(const char *ta,
                                       const char *if_name,
                                       const char *feature_name,
                                       int req_status);


/**
 * Send some data, receive it on peer, check it.
 * In case of any failure, print verdict and stop the test.
 *
 * @param rpcs_sender       RPC server from which to send.
 * @param s_sender          Socket from which to send.
 * @param rpcs_receiver     RPC server on which to receive.
 * @param s_receiver        Socket on which to receive.
 * @param vpref             Prefix for verdicts.
 *
 * @return Number of bytes sent and received.
 */
extern size_t net_drv_send_recv_check(rcf_rpc_server *rpcs_sender,
                                      int s_sender,
                                      rcf_rpc_server *rpcs_receiver,
                                      int s_receiver,
                                      const char *vpref);

/**
 * Same as net_drv_send_recv_check() but allows to specify destination
 * address.
 *
 * @param rpcs_sender       RPC server from which to send.
 * @param s_sender          Socket from which to send.
 * @param dst_addr          If not @c NULL, @b sendto() should
 *                          be called with this destination.
 * @param rpcs_receiver     RPC server on which to receive.
 * @param s_receiver        Socket on which to receive.
 * @param vpref             Prefix for verdicts.
 *
 * @return Number of bytes sent and received.
 */
extern size_t net_drv_sendto_recv_check(rcf_rpc_server *rpcs_sender,
                                        int s_sender,
                                        const struct sockaddr *dst_addr,
                                        rcf_rpc_server *rpcs_receiver,
                                        int s_receiver,
                                        const char *vpref);

/**
 * Same as net_drv_sendto_recv_check() but do not fail if receiver
 * does not become readable and return 0.
 *
 * @param rpcs_sender       RPC server from which to send.
 * @param s_sender          Socket from which to send.
 * @param dst_addr          If not @c NULL, @b sendto() should
 *                          be called with this destination.
 * @param rpcs_receiver     RPC server on which to receive.
 * @param s_receiver        Socket on which to receive.
 * @param vpref             Prefix for verdicts.
 *
 * @return Number of bytes sent and received.
 */
extern size_t net_drv_sendto_recv_check_may_loss(
                    rcf_rpc_server *rpcs_sender,
                    int s_sender,
                    const struct sockaddr *dst_addr,
                    rcf_rpc_server *rpcs_receiver,
                    int s_receiver,
                    const char *vpref);

/**
 * Send data in both directions between a pair of connected
 * sockets, check that it can be received.
 * In case of any failure, print verdict and stop the test.
 *
 * @param rpcs1       The first RPC server.
 * @param s1          The first socket.
 * @param s1_name     Name of the first socket to be used in
 *                    verdicts.
 * @param rpcs2       The second RPC server.
 * @param s2          The second socket.
 * @param s2_name     Name of the second socket to be used in
 *                    verdicts.
 * @param vpref       Common prefix for verdicts.
 */
extern void net_drv_conn_check(rcf_rpc_server *rpcs1,
                               int s1,
                               const char *s1_name,
                               rcf_rpc_server *rpcs2,
                               int s2,
                               const char *s2_name,
                               const char *vpref);

/**
 * Find all the readable files under a specified path and log their
 * contents. The function will report failure if there is no matching
 * files at all.
 *
 * @param rpcs          RPC server.
 * @param timeout       Timeout for logging all files call, if it is @c 0
 *                      then use default
 * @param path_fmt...   Path format string and arguments.
 *
 * @return Status code.
 */
extern te_errno net_drv_cat_all_files(rcf_rpc_server *rpcs,
                                      uint32_t timeout,
                                      const char *path_fmt, ...);

/**
 * Set a new MAC address on a network interface, check that the
 * new address is reported for the interface in configuration tree
 * after that.
 *
 * @param ta        Test Agent name.
 * @param if_name   Interface name.
 * @param mac_addr  Address to set (assumed to have @c ETHER_ADDR_LEN
 *                  bytes).
 *
 * @return Status code.
 */
extern te_errno net_drv_set_check_mac(const char *ta,
                                      const char *if_name,
                                      const void *mac_addr);

/**
 * Try to set MTU, print verdict if it fails.
 *
 * @param ta        Test Agent
 * @param if_name   Interface name
 * @param mtu       MTU value to set
 * @param where     Where MTU is set (to print in verdict)
 */
extern void net_drv_set_mtu(const char *ta, const char *if_name,
                            int mtu, const char *where);

/**
 * Get number of /agent:<name>/sys:/net:/ipv[46]:/neigh:<ifname>
 * nodes currently present in configuration tree. This function
 * is intended to be used with net_drv_wait_neigh_nodes_recover().
 *
 * @param ta      Test Agent name
 * @param num     Where to save number of neigh nodes
 *
 * @return Status code.
 */
extern te_errno net_drv_neigh_nodes_count(const char *ta,
                                          unsigned int *num);

/**
 * Wait until the number of neigh nodes in configuration tree is
 * not less than a given number.
 * This function can be used to check that interfaces are fully
 * recovered after the driver is reloaded. For some reason sometimes
 * it takes more time for all neigh nodes to reappear, and
 * restoring configuration from backup fails in such case.
 *
 * @param ta        Test Agent name
 * @param num       Number of neigh nodes which should be present
 *
 * @return Status code.
 */
extern te_errno net_drv_wait_neigh_nodes_recover(const char *ta,
                                                 unsigned int num);

/**
 * Set PCI device parameter to a given value and check that its
 * value is expected after that. In case of failure this function
 * prints verdict and jumps to cleanup.
 *
 * @param pci_oid       PCI device OID
 * @param param_name    Parameter name
 * @param cmode         Configuration mode in which value should be set
 * @param value         Value to set
 * @param vpref         Prefix to print in verdicts in case of failure
 */
extern void net_drv_set_pci_param_uint(const char *pci_oid,
                                       const char *param_name,
                                       tapi_cfg_pci_param_cmode cmode,
                                       uint64_t value,
                                       const char *vpref);

/**
 * Wait until link status for the IUT interface is reported to be UP in
 * configuration tree.
 *
 * @param ta        Test Agent name
 * @param if_name   Interface name
 * @oaram cleanup   Whether to use verdict suitable for test section or
 *                  for cleanup section
 */
extern void net_drv_wait_up_gen(const char *ta, const char *if_name,
                                te_bool cleanup);

static inline void net_drv_wait_up(const char *ta, const char *if_name)
{
    return net_drv_wait_up_gen(ta, if_name, FALSE);
}

/**
 * Cleanup macro for bringing up interface and waiting until link status
 * for the IUT interface is reported to be UP in configuration tree.
 *
 * @param ta        Test Agent name
 * @param if_name   Interface name
 */
#define NET_DRV_CLEANUP_SET_UP_WAIT(_ta, _if_name) \
do {                                                      \
    CLEANUP_CHECK_RC(tapi_cfg_base_if_up(_ta, _if_name)); \
    CFG_WAIT_CHANGES;                                     \
    net_drv_wait_up_gen(_ta, _if_name, TRUE);             \
} while (0)

/**
 * Create VLAN over pair of interfaces, allocate and assign IP
 * addresses, use the same transport ports.
 *
 * @param[in] ta1           The first Test Agent.
 * @param[in] ta2           The second Test Agent.
 * @param[in] if1           Interface on the first Test Agent.
 * @param[in] if2           Interface on the second Test Agent.
 * @param[in] af            Family of addresses to add.
 * @param[in,out] vlan_id   Optional location with/for VLAN ID
 *                          (if not @c NULL and value is not @c 0,
 *                          use specified ID, otherwise allocate random
 *                          and return it here).
 * @param[out] vlan_addr1   Optional location for address assigned to
 *                          the VLAN over the first interface.
 * @param[out] vlan_addr2   Optional location for address assigned to
 *                          the VLAN over the second interface.
 */
extern void net_drv_ts_add_vlan(const char *ta1, const char *ta2,
                                const char *if1, const char *if2,
                                int af, uint16_t *vlan_id,
                                struct sockaddr **vlan_addr1,
                                struct sockaddr **vlan_addr2);

#endif /* !__TS_NET_DRV_TS_H__ */
