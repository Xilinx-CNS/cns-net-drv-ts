/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Common test API
 *
 * Declarations of TAPI for checking ethtool.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#ifndef __TS_NET_DRV_ETHTOOL_H__
#define __TS_NET_DRV_ETHTOOL_H__

#include "te_config.h"

#include "te_defs.h"
#include "rcf_rpc.h"

/** List of ETHTOOL_RESET flags for TEST_GET_ENUM_PARAM() */
#define NET_DRV_RESET_FLAGS \
    { "none", 0 }, \
    { "MGMT", RPC_ETH_RESET_MGMT }, \
    { "IRQ", RPC_ETH_RESET_IRQ }, \
    { "DMA", RPC_ETH_RESET_DMA }, \
    { "FILTER", RPC_ETH_RESET_FILTER }, \
    { "OFFLOAD", RPC_ETH_RESET_OFFLOAD }, \
    { "MAC", RPC_ETH_RESET_MAC }, \
    { "PHY", RPC_ETH_RESET_PHY }, \
    { "RAM", RPC_ETH_RESET_RAM }, \
    { "SHARED_MGMT", RPC_ETH_RESET_SHARED_MGMT }, \
    { "SHARED_IRQ", RPC_ETH_RESET_SHARED_IRQ }, \
    { "SHARED_DMA", RPC_ETH_RESET_SHARED_DMA }, \
    { "SHARED_FILTER", RPC_ETH_RESET_SHARED_FILTER }, \
    { "SHARED_OFFLOAD", RPC_ETH_RESET_SHARED_OFFLOAD }, \
    { "SHARED_MAC", RPC_ETH_RESET_SHARED_MAC }, \
    { "SHARED_PHY", RPC_ETH_RESET_SHARED_PHY }, \
    { "SHARED_RAM", RPC_ETH_RESET_SHARED_RAM }, \
    { "DEDICATED", RPC_ETH_RESET_DEDICATED }, \
    { "ALL", RPC_ETH_RESET_ALL }

/**
 * Call SIOCETHTOOL/ETHTOOL_RESET.
 * This is simple wrapper over rpc_ioctl(); RPC_AWAIT_ERROR() can be used
 * with it.
 *
 * @param rpcs            RPC server
 * @param s               Socket FD
 * @param if_name         Interface name
 * @param flags           Reset flags (see @ref rpc_ethtool_reset_flags)
 * @param ret_flags       If not @c NULL, flags not cleared by ioctl() will
 *                        be saved here
 *
 * @return Result of rpc_ioctl().
 */
extern int net_drv_ethtool_reset(rcf_rpc_server *rpcs, int s,
                                 const char *if_name,
                                 unsigned int flags,
                                 unsigned int *ret_flags);

/**
 * Check whether CLI options used by TS tests are present in
 * @b ethtool --help output and add proper TRC tags if some are missing.
 *
 * @param rpcs            RPC server.
 *
 * @return Status code.
 */
extern te_errno net_drv_add_missing_ethtool_opt_tags(rcf_rpc_server *rpcs);

#endif /* !__TS_NET_DRV_ETHTOOL_H__ */
