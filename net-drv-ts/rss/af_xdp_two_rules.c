/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2023 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-af_xdp_two_rules Two Rx rules and AF_XDP sockets
 * @ingroup rss
 * @{
 *
 * @objective Check that two AF_XDP sockets can work simultaneously
 *            each receiving packets from a different Rx queue specified
 *            by a Rx classification rule.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param copy_mode      XDP copy mode:
 *                       - @c none (kernel tries zero-copy, falls back to
 *                         copy mode if it fails)
 *                       - @c copy
 *                       - @c zerocopy
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "rss/af_xdp_two_rules"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "te_rand.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_rpc_bpf.h"
#include "tapi_cfg_rx_rule.h"
#include "common_rss.h"

/* Maximum number of attempts to choose new port */
#define MAX_ATTEMPTS 100000

/*
 * Choose a port on IUT such that packets sent from Tester to that port
 * will not be directed to a given Rx queue according to RSS indirection
 * table.
 */
static void
get_iut_addr(net_drv_rss_ctx *rss_ctx,
             const struct sockaddr *iut_addr,
             const struct sockaddr *tst_addr,
             unsigned int rule_queue, unsigned int exclude_port,
             struct sockaddr *new_iut_addr)
{
    unsigned int i;
    unsigned int port;
    unsigned int queue_id;

    tapi_sockaddr_clone_exact(iut_addr,
                              (struct sockaddr_storage *)new_iut_addr);

    for (i = 0; i < MAX_ATTEMPTS; i++)
    {
        port = tapi_get_random_port();
        if (port == exclude_port)
            continue;
        te_sockaddr_set_port(new_iut_addr, htons(port));

        CHECK_RC(net_drv_rss_predict(rss_ctx, tst_addr, new_iut_addr,
                                     NULL, NULL, &queue_id));
        if (queue_id != rule_queue)
            break;
    }

    if (i == MAX_ATTEMPTS)
    {
        TEST_FAIL("Failed to find IUT port for which incomping packets are "
                  "not directed to queue %u by default", rule_queue);
    }

    RING("By default Rx queue %u should be used for packets sent to %s",
         queue_id, sockaddr_h2str(new_iut_addr));
}

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;
    bool multi_seg;
    unsigned int copy_mode;

    struct sockaddr_storage iut_addr1_st;
    struct sockaddr_storage iut_addr2_st;
    struct sockaddr *iut_addr1 = SA(&iut_addr1_st);
    struct sockaddr *iut_addr2 = SA(&iut_addr2_st);
    struct sockaddr_storage dst_addr;

    net_drv_rss_ctx rss_ctx = NET_DRV_RSS_CTX_INIT;
    unsigned int bpf_id;
    int map_fd = -1;
    int tst_s = -1;
    net_drv_xdp_cfg xdp_cfg = NET_DRV_XDP_CFG_DEF;
    net_drv_xdp_sock xdp_socks[2] = { NET_DRV_XDP_SOCK_INIT,
                                      NET_DRV_XDP_SOCK_INIT };

    unsigned int queue1;
    unsigned int queue2;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BOOL_PARAM(multi_seg);
    TEST_GET_ENUM_PARAM(copy_mode, NET_DRV_XDP_COPY_MODE);

    CHECK_RC(net_drv_xdp_adjust_rx_size(iut_rpcs->ta, iut_if->if_name,
                                        &xdp_cfg));
    CFG_WAIT_CHANGES;

    net_drv_rss_ctx_prepare(&rss_ctx, iut_rpcs->ta, iut_if->if_name, 0);
    if (rss_ctx.rx_queues < 2)
        TEST_FAIL("At least two Rx queues should be available");

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    TEST_STEP("From IUT TA pin XSK BPF map of @b rxq_stats XDP program to "
              "a file. From IUT RPC server open that file to obtain file "
              "descriptor of the map.");
    CHECK_RC(tapi_bpf_map_pin_get_fd(iut_rpcs, bpf_id,
                                     TAPI_BPF_RXQ_STATS_XSK_MAP,
                                     &map_fd));

    TEST_STEP("Choose two different Rx queues: @b queue1 and @b queue2.");
    queue1 = rand_range(0, rss_ctx.rx_queues - 1);
    queue2 = te_rand_range_exclude(0, rss_ctx.rx_queues - 1, queue1);
    RING("Rx queues %u and %u are chosen", queue1, queue2);

    TEST_STEP("Construct @b iut_addr1 with address taken from @p iut_addr "
              "and a random port such that packets sent to it from "
              "@p tst_addr will not be received by @b queue1 by default.");
    get_iut_addr(&rss_ctx, iut_addr, tst_addr, queue1, 0, iut_addr1);

    TEST_STEP("Construct @b iut_addr2 with address taken from @p iut_addr "
              "and a random port such that packets sent to it from "
              "@p tst_addr will not be received by @b queue2 by default.");
    get_iut_addr(&rss_ctx, iut_addr, tst_addr, queue2,
                 te_sockaddr_get_port(iut_addr1), iut_addr2);

    TEST_STEP("Create UDP socket on Tester, bind it to @p tst_addr.");
    tst_s = rpc_create_and_bind_socket(tst_rpcs, RPC_SOCK_DGRAM,
                                       RPC_PROTO_DEF, FALSE, FALSE,
                                       tst_addr);

    TEST_STEP("Create two AF_XDP sockets on IUT, the first one bound to "
              "@b queue1, and the second one bound to @b queue2.");
    xdp_cfg.bind_flags = copy_mode |
                         (multi_seg ? RPC_XDP_BIND_USE_SG : 0);
    CHECK_RC(net_drv_xdp_create_sock(iut_rpcs, iut_if->if_name, queue1,
                                     &xdp_cfg, map_fd, &xdp_socks[0]));
    CHECK_RC(net_drv_xdp_create_sock(iut_rpcs, iut_if->if_name, queue2,
                                     &xdp_cfg, map_fd, &xdp_socks[1]));
    NET_DRV_XDP_WAIT_SOCKS;

    TEST_STEP("Add Rx rule on IUT to redirect packets sent from "
              "@p tst_addr to @b iut_addr1 to Rx queue @b queue1.");
    net_drv_add_tcpudp_rx_rule(iut_rpcs->ta, iut_if->if_name,
                               RPC_SOCK_DGRAM,
                               tst_addr, NULL, iut_addr1, NULL,
                               queue1, "the first");

    TEST_STEP("Add Rx rule on IUT to redirect packets sent from "
              "@p tst_addr to @b iut_addr2 to Rx queue @b queue2.");
    net_drv_add_tcpudp_rx_rule(iut_rpcs->ta, iut_if->if_name,
                               RPC_SOCK_DGRAM,
                               tst_addr, NULL, iut_addr2, NULL,
                               queue2, "the second");

    CFG_WAIT_CHANGES;

    tapi_sockaddr_clone_exact(iut_addr, &dst_addr);
    te_sockaddr_set_port(SA(&dst_addr), 0);

    TEST_STEP("Configure @b rxq_stats program on IUT to process only "
              "packets going from @p tst_addr to @p iut_addr (accepting "
              "any destination port).");
    CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));
    CHECK_RC(tapi_bpf_rxq_stats_set_params(
                  iut_rpcs->ta, bpf_id, iut_addr->sa_family,
                  tst_addr, SA(&dst_addr), IPPROTO_UDP, TRUE));

    TEST_STEP("Check that the first AF_XDP socket can receive a UDP packet "
              "sent from @p tst_addr to @b iut_addr1 and reply over a "
              "Tx-capable AF_XDP socket.");
    net_drv_xdp_echo(tst_rpcs, tst_s, iut_addr1,
                     iut_rpcs, xdp_socks, 2, 0, multi_seg);

    TEST_STEP("Check that the second AF_XDP socket can receive a UDP packet "
              "sent from @p tst_addr to @b iut_addr2 and reply over a "
              "Tx-capable AF_XDP socket.");
    net_drv_xdp_echo(tst_rpcs, tst_s, iut_addr2,
                     iut_rpcs, xdp_socks, 2, 1, multi_seg);

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(net_drv_xdp_destroy_sock(iut_rpcs, &xdp_socks[0]));
    CLEANUP_CHECK_RC(net_drv_xdp_destroy_sock(iut_rpcs, &xdp_socks[1]));
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    net_drv_rss_ctx_release(&rss_ctx);
    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    TEST_END;
}
