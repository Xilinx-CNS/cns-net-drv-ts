/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Test API for checking PTP
 *
 * Declarations of TAPI for PTP tests.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#ifndef __TS_NET_DRV_PTP_H__
#define __TS_NET_DRV_PTP_H__

#include "te_config.h"
#include "te_defs.h"
#include "rcf_rpc.h"

/**
 * Maximum inaccuracy in seconds when checking what clock_gettime()
 * returns. It is assumed that this inaccuracy is largely due to TE
 * delays (time required to call RPCs, etc).
 */
#define NET_DRV_DEF_PTP_INACC 0.1

/**
 * Maximum deviation from average time difference between PTP and system
 * clocks when checking requests like PTP_SYS_OFFSET, in seconds.
 */
#define NET_DRV_PTP_OFFS_MAX_DEV_FROM_AVG 0.001

/*
 * Maximum difference between offset measured with PTP_SYS_OFFSET-like
 * requests and offset measured with help of two clock_gettime() calls,
 * when checking difference between PTP and system clocks, in seconds.
 */
#define NET_DRV_PTP_OFFS_MAX_DEV_FROM_GETTIME 0.5

/**
 * Open PTP device associated with a given network interface.
 * This function prints verdict and terminates test on failure.
 *
 * @param rpcs      RPC server
 * @param if_name   Interface name
 * @param fd        Where to save opened file descriptor
 * @param vpref     Prefix for verdicts (may be @c NULL or empty)
 */
extern void net_drv_open_ptp_fd(rcf_rpc_server *rpcs, const char *if_name,
                                int *fd, const char *vpref);

/**
 * Check PTP support for the interface.
 *
 * @param rpcs      RPC server.
 * @param if_name   Interface name.
 *
 * @return @c true if PTP support is available, @c false otherwise.
 */
extern bool net_drv_is_ptp_supported(rcf_rpc_server *rpcs,
                                     const char *if_name);

/**
 * Get difference between two timespec structures in seconds
 * (tsa - tsb).
 *
 * @param tsa     The first timestamp
 * @param tsb     The second timestamp
 *
 * @return Difference in seconds.
 */
extern double net_drv_timespec_diff(tarpc_timespec *tsa,
                                    tarpc_timespec *tsb);

/**
 * Get difference between two tarpc_ptp_clock_time structures in seconds
 * (tsa - tsb).
 *
 * @param tsa     The first timestamp
 * @param tsb     The second timestamp
 *
 * @return Difference in seconds.
 */
extern double net_drv_ptp_clock_time_diff(tarpc_ptp_clock_time *tsa,
                                          tarpc_ptp_clock_time *tsb);

/**
 * Check whether sample differences are close to the average
 * when testing requests like PTP_SYS_OFFSET.
 *
 * @param values          Array of differences between clocks
 * @param number          Number of elements in the array
 * @param avg_diff        Average difference
 */
extern void net_drv_ptp_offs_check_dev_avg(double *values,
                                           unsigned int number,
                                           double avg_diff);

/**
 * Check whether estimated difference between PTP clock and system clock
 * is close to the difference computed with two consecutive
 * clock_gettime() calls.
 *
 * @param rpcs        RPC server on which to call clock_gettime()
 * @param ptp_fd      PTP device FD
 * @param sys_clock   System clock to get system timestamp from
 * @param ptp_offs    Estimated difference between PTP and system clock,
 *                    in seconds
 */
extern void net_drv_ptp_offs_check_dev_gettime(rcf_rpc_server *rpcs,
                                               int ptp_fd,
                                               rpc_clock_id sys_clock,
                                               double ptp_offs);

#endif /* !__TS_NET_DRV_PTP_H__ */
