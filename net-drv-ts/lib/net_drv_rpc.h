/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2023 OKTET Labs Ltd. All rights reserved. */
/** @file
 * @brief RPC calls
 *
 * Declarations of test suite speficic RPC calls.
 */

#ifndef __TS_NET_DRV_RPC_H__
#define __TS_NET_DRV_RPC_H__

#include "te_config.h"

#include "rcf_rpc.h"
#include "te_rpc_types.h"

/**
 * Create as many Rx classification rules as possible before it fails.
 * Then remove all the created rules.
 *
 * @note To make rules different, this function iterates over all possible
 *       TCP/UDP ports for source and destination. So this function can
 *       create up to 4294836225 rules.
 *
 * @param rpcs          RPC server.
 * @param fd            File descriptor on which to call ioctl() when
 *                      creating Rx rules.
 * @param if_name       Interface name.
 * @param src_addr      Source IP address.
 * @param dst_addr      Destination IP address.
 * @param sock_type     @c RPC_SOCK_STREAM or @c RPC_SOCK_DGRAM -
 *                      defines whether created rule is for TCP or
 *                      for UDP.
 * @param any_location  If @c TRUE, use special "any" location when adding
 *                      a rule. Otherwise set specific location.
 * @param queues_num    Number of available Rx queues.
 * @param rules_count   How many rules were created.
 * @param add_error     Which error was encountered when trying to add the
 *                      last rule.
 *
 * @return @c 0 on success, @c -1 on failure.
 */
extern int rpc_net_drv_too_many_rx_rules(rcf_rpc_server *rpcs,
                                         int fd,
                                         const char *if_name,
                                         const struct sockaddr *src_addr,
                                         const struct sockaddr *dst_addr,
                                         rpc_socket_type sock_type,
                                         te_bool any_location,
                                         unsigned int queues_num,
                                         unsigned int *rules_count,
                                         te_errno *add_errno);

/**
 * Send packets trying to keep requested time intervals between them.
 *
 * @note This is intended to be used with datagram sockets. Every packet has
 *       payload of 9 bytes, the first 8 bytes is 64-bit packet ordinal
 *       number in network byte order (to make it possible to detect lost
 *       packets). The last byte is 0xff, it makes it easy to skip small
 *       Ethernet frame padding at the end if it is retrieved by CSAP.
 *
 * @param rpcs      RPC server.
 * @param s         Socket FD.
 * @param delay     Delay between packets, in microseconds.
 * @param time2run  How long to send packets, in milliseconds.
 *
 * @return Number of sent packets on success, @c -1 on failure.
 */
extern int64_t rpc_net_drv_send_pkts_exact_delay(rcf_rpc_server *rpcs,
                                                 int s,
                                                 unsigned int delay,
                                                 unsigned int time2run);

/**
 * Receive and check packets sent with rpc_net_drv_send_pkts_exact_delay().
 *
 * @note This function allows for packet losses.
 *
 * @param rpcs        RPC server.
 * @param s           Socket FD.
 * @param time2wait   How long to wait for new data, in milliseconds.
 *                    If no new data comes during this time, the RPC
 *                    call terminates.
 *
 * @return Number of received packets on success, @c -1 on failure.
 */
extern int64_t rpc_net_drv_recv_pkts_exact_delay(rcf_rpc_server *rpcs,
                                                 int s,
                                                 unsigned int time2wait);

#endif /* !__TS_NET_DRV_RPC_H__ */
