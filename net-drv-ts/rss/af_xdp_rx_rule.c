/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2023 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-af_xdp_rx_rule Rx classification rule and AF_XDP socket
 * @ingroup rss
 * @{
 *
 * @objective Check that a packet directed to Rx queue by Rx classification
 *            rule can be received and replied by AF_XDP socket.
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

#define TE_TEST_NAME "rss/af_xdp_rx_rule"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "te_rand.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_rpc_bpf.h"
#include "tapi_cfg_rx_rule.h"
#include "common_rss.h"

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    net_drv_xdp_cfg xdp_cfg = NET_DRV_XDP_CFG_DEF;
    bool multi_seg;
    unsigned int copy_mode;
    net_drv_xdp_sock xdp_sock = NET_DRV_XDP_SOCK_INIT;

    net_drv_rss_ctx rss_ctx = NET_DRV_RSS_CTX_INIT;
    unsigned int bpf_id;
    unsigned int queue_id;
    unsigned int rule_queue;

    int tst_s = -1;
    int map_fd = -1;

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

    TEST_STEP("Create UDP socket on Tester, bind it to @p tst_addr.");
    tst_s = rpc_create_and_bind_socket(tst_rpcs, RPC_SOCK_DGRAM,
                                       RPC_PROTO_DEF, FALSE, FALSE,
                                       tst_addr);

    net_drv_rss_ctx_prepare(&rss_ctx, iut_rpcs->ta, iut_if->if_name, 0);

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    TEST_STEP("From IUT TA pin XSK BPF map of @b rxq_stats XDP program to "
              "a file. From IUT RPC server open that file to obtain file "
              "descriptor of the map.");
    CHECK_RC(tapi_bpf_map_pin_get_fd(iut_rpcs, bpf_id,
                                     TAPI_BPF_RXQ_STATS_XSK_MAP,
                                     &map_fd));

    TEST_STEP("Find out which Rx queue on IUT will be used for packets "
              "sent from @p tst_addr to @p iut_addr according to "
              "RSS indirection table.");
    CHECK_RC(net_drv_rss_predict(&rss_ctx, tst_addr, iut_addr,
                                 NULL, NULL, &queue_id));
    RING("Predicted Rx queue %u", queue_id);

    TEST_STEP("Choose a different Rx queue @b rule_queue on IUT.");
    if (rss_ctx.rx_queues <= 1)
        TEST_FAIL("No alternative Rx queue is available");
    rule_queue = te_rand_range_exclude(0, rss_ctx.rx_queues - 1, queue_id);
    RING("Packets will be redirected to queue %u", rule_queue);

    TEST_STEP("Create Rx classification rule on IUT redirecting "
              "packets sent from @p tst_addr to @p iut_addr to "
              "Rx queue @b rule_queue.");

    net_drv_add_tcpudp_rx_rule(iut_rpcs->ta, iut_if->if_name,
                               RPC_SOCK_DGRAM, tst_addr, NULL,
                               iut_addr, NULL, rule_queue, "");
    CFG_WAIT_CHANGES;

    TEST_STEP("Configure @b rxq_stats XDP program on IUT to process only "
              "packets going from @p tst_addr to @p iut_addr.");
    CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));
    CHECK_RC(tapi_bpf_rxq_stats_set_params(
                  iut_rpcs->ta, bpf_id, iut_addr->sa_family,
                  tst_addr, iut_addr, IPPROTO_UDP, TRUE));

    TEST_STEP("Create AF_XDP socket on IUT bound to @b rule_queue. "
              "Set corresponding record of XSK map of @b rxq_stats program "
              "to its file descriptor.");
    xdp_cfg.bind_flags = copy_mode |
                         (multi_seg ? RPC_XDP_BIND_USE_SG : 0);
    CHECK_RC(net_drv_xdp_create_sock(iut_rpcs, iut_if->if_name, rule_queue,
                                     &xdp_cfg, map_fd, &xdp_sock));
    NET_DRV_XDP_WAIT_SOCKS;

    TEST_STEP("Send a packet from the Tester UDP socket. Check that the "
              "AF_XDP socket gets that packet on IUT. Construct a reply "
              "by inverting addresses and ports of the received packet. "
              "Send it back to Tester over a Tx-capable AF_XDP socket. "
              "Check that the Tester socket gets it and its payload "
              "is the same as in the previously sent packet.");
    net_drv_xdp_echo(tst_rpcs, tst_s, iut_addr,
                     iut_rpcs, &xdp_sock, 1, 0, multi_seg);

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(net_drv_xdp_destroy_sock(iut_rpcs, &xdp_sock));

    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    net_drv_rss_ctx_release(&rss_ctx);
    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    TEST_END;
}
