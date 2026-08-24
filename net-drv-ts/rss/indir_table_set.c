/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-indir_table_set Changing RSS hash indirection table.
 * @ingroup rss
 * @{
 *
 * @objective Check that a record of RSS hash indirection table can be
 *            changed to specify another Rx queue and the new queue will
 *            then be used for an existing connection whose hash is mapped
 *            to that table record.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param sock_type      Socket type:
 *                       - @c SOCK_STREAM
 *                       - @c SOCK_DGRAM
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "rss/indir_table_set"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "common_rss.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;
    rpc_socket_type sock_type;
    bool multi_seg;

    int iut_s = -1;
    int tst_s = -1;

    unsigned int idx;
    unsigned int cur_queue;
    unsigned int new_queue;
    unsigned int bpf_id = 0;

    unsigned int hash;
    te_bool test_failed = FALSE;

    net_drv_rss_ctx ctx = NET_DRV_RSS_CTX_INIT;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BOOL_PARAM(multi_seg);
    TEST_GET_SOCK_TYPE(sock_type);

    net_drv_rss_ctx_prepare(&ctx, iut_rpcs->ta, iut_if->if_name, 0);

    CHECK_RC(tapi_cfg_if_rss_print_indir_table(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    TEST_STEP("Create a pair of connected sockets of type @p sock_type "
              "on IUT and Tester.");

    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Compute Toeplitz hash for packets sent from Tester to find "
              "out which indirection table record specifies Rx queue for "
              "those packets.");

    CHECK_RC(net_drv_rss_predict(&ctx, tst_addr, iut_addr,
                                 &hash, &idx, &cur_queue));

    TEST_STEP("Configure XDP hook to count only packets going "
              "from Tester to IUT via the created connection. It will "
              "tell which Rx queues processed these packets.");

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    CHECK_RC(
        tapi_bpf_rxq_stats_set_params(
             iut_rpcs->ta, bpf_id, iut_addr->sa_family,
             tst_addr, iut_addr,
             sock_type == RPC_SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP,
             TRUE));

    TEST_STEP("Send a few packets from the Tester socket. Check that they "
              "went via the specified Rx queue.");

    RING("With current hash key hash value is expected to be 0x%x "
         "and packets should go via queue %u specified "
         "in indirection table entry %u", hash, cur_queue, idx);

    rc = net_drv_rss_send_check_stats(tst_rpcs, tst_s, NULL,
                                      iut_rpcs, iut_s, NULL,
                                      sock_type, cur_queue, bpf_id,
                                      multi_seg,
                                      "Before indirection table change");
    if (rc != 0)
        test_failed = TRUE;

    TEST_STEP("Change Rx queue in the indirection table record.");

    new_queue = (cur_queue + 1) % ctx.rx_queues;

    CHECK_RC(tapi_cfg_if_rss_indir_set_local(iut_rpcs->ta, iut_if->if_name,
                                             0, idx, new_queue));
    CHECK_RC(tapi_cfg_if_rss_hash_indir_commit(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    TEST_STEP("Send a few packets from the Tester socket. Check that they "
              "went via the new Rx queue.");

    RING("After indirection table change packets should go via queue %u",
         new_queue);

    CHECK_RC(net_drv_rss_send_check_stats(
                                    tst_rpcs, tst_s, NULL,
                                    iut_rpcs, iut_s, NULL,
                                    sock_type, new_queue, bpf_id, multi_seg,
                                    "After indirection table change"));

    if (test_failed)
        TEST_STOP;
    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    net_drv_rss_ctx_release(&ctx);

    TEST_END;
}
