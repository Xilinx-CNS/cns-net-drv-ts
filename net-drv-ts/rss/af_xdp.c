/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2023 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-af_xdp Check sending and receiving with AF_XDP sockets
 * @ingroup rss
 * @{
 *
 * @objective Check that AF_XDP sockets can receive and send packets.
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

#define TE_TEST_NAME "rss/af_xdp"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_rpc_bpf.h"
#include "common_rss.h"

/* How many times to try to choose port number */
#define MAX_CHANGE_PORT_ATTEMPTS 100000

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;
    struct sockaddr_storage dst_addr;

    net_drv_xdp_cfg xdp_cfg = NET_DRV_XDP_CFG_DEF;
    bool multi_seg;
    unsigned int copy_mode;
    net_drv_xdp_sock *socks = NULL;
    unsigned int socks_num = 0;

    net_drv_rss_ctx rss_ctx = NET_DRV_RSS_CTX_INIT;
    unsigned int bpf_id;

    unsigned int i;
    unsigned int j;
    unsigned int queue_id;
    unsigned int port;

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

    TEST_STEP("Make sure that RSS indirection table mentions all the "
              "available Rx queues on IUT.");
    CHECK_RC(tapi_cfg_if_rss_fill_indir_table(iut_rpcs->ta, iut_if->if_name,
                                              0, 0, rss_ctx.rx_queues - 1));
    CHECK_RC(tapi_cfg_if_rss_hash_indir_commit(iut_rpcs->ta,
                                               iut_if->if_name, 0));
    CHECK_RC(tapi_cfg_if_rss_print_indir_table(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    TEST_STEP("From IUT TA pin XSK BPF map of @b rxq_stats XDP program to "
              "a file. From IUT RPC server open that file to obtain file "
              "descriptor of the map.");
    CHECK_RC(tapi_bpf_map_pin_get_fd(iut_rpcs, bpf_id,
                                     TAPI_BPF_RXQ_STATS_XSK_MAP,
                                     &map_fd));

    TEST_STEP("Create AF_XDP sockets on IUT, one per each Rx queue. "
              "Set XDP copy mode according to @p copy_mode when binding "
              "the sockets. Set corresponding entries of the XSK map to "
              "file desciptors of the created sockets.");
    socks_num = rss_ctx.rx_queues;
    xdp_cfg.bind_flags = copy_mode |
                         (multi_seg ? RPC_XDP_BIND_USE_SG : 0);
    net_drv_xdp_create_socks(iut_rpcs, iut_if->if_name,
                             &xdp_cfg, map_fd, socks_num, &socks);
    NET_DRV_XDP_WAIT_SOCKS;

    tapi_sockaddr_clone_exact(iut_addr, &dst_addr);
    te_sockaddr_set_port(SA(&dst_addr), 0);

    TEST_STEP("Configure @b rxq_stats program on IUT to process only "
              "packets going from @p tst_addr to @p iut_addr (accepting "
              "any destination port).");
    CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));
    CHECK_RC(tapi_bpf_rxq_stats_set_params(
                  iut_rpcs->ta, bpf_id, iut_addr->sa_family,
                  tst_addr, SA(&dst_addr), IPPROTO_UDP, TRUE));

    TEST_STEP("For every Rx queue on IUT:");
    for (i = 0; i < socks_num; i++)
    {
        TEST_SUBSTEP("Find the destination IUT port such that with "
                     "that port packets sent from @p tst_addr will be "
                     "directed to the tested Rx queue.");
        for (j = 0; j < MAX_CHANGE_PORT_ATTEMPTS; j++)
        {
            port = tapi_get_random_port();
            te_sockaddr_set_port(SA(&dst_addr), htons(port));

            CHECK_RC(net_drv_rss_predict(&rss_ctx, tst_addr, SA(&dst_addr),
                                         NULL, NULL, &queue_id));
            if (queue_id == i)
                break;
        }
        if (j == MAX_CHANGE_PORT_ATTEMPTS)
            TEST_FAIL("Failed to find a port matching queue %u", i);

        TEST_SUBSTEP("Send a packet from the Tester socket. Check that the "
                     "AF_XDP socket assigned to the tested Rx queue gets "
                     "that packet. Construct a reply by inverting "
                     "addresses and ports of the received packet. Send it "
                     "back to Tester over a Tx-capable AF_XDP socket. "
                     "Check that the Tester socket gets it and its payload "
                     "is the same as in the previously sent packet.");
        net_drv_xdp_echo(tst_rpcs, tst_s, SA(&dst_addr),
                         iut_rpcs, socks, socks_num, i, multi_seg);
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(net_drv_xdp_destroy_socks(iut_rpcs, socks, socks_num));

    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    net_drv_rss_ctx_release(&rss_ctx);
    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    TEST_END;
}
