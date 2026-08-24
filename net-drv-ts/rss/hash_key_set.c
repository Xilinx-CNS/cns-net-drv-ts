/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-hash_key_set Changing RSS hash key.
 * @ingroup rss
 * @{
 *
 * @objective Check that RSS hash key change can result in changing
 *            of Rx queue which processes packets of a given connection.
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

#define TE_TEST_NAME "rss/hash_key_set"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_mem.h"
#include "common_rss.h"

/* Maximum number of attempts to find a new hash key */
#define MAX_ATTEMPTS 10000

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

    unsigned int hash;
    uint8_t *new_key = NULL;

    unsigned int idx;
    unsigned int i;
    unsigned int j;
    unsigned int bpf_id = 0;

    unsigned int init_queue;
    unsigned int new_queue;
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

    new_key = tapi_calloc(ctx.key_len, 1);

    CHECK_RC(tapi_cfg_if_rss_print_indir_table(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    CHECK_RC(net_drv_rss_predict(&ctx, tst_addr, iut_addr,
                                 &hash, &idx, &init_queue));

    TEST_STEP("Create a pair of connected sockets of type @p sock_type "
              "on IUT and Tester.");

    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Send a few packets from Tester over the created connection. "
              "Check that XDP hook reports that all packets go through the "
              "Rx queue determined by the current hash key value.");

    RING("With current hash key hash value should be 0x%x and packets "
         "should go via queue %u specified in indirection table entry %u",
         hash, init_queue, idx);

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    CHECK_RC(
            tapi_bpf_rxq_stats_set_params(
                 iut_rpcs->ta, bpf_id, iut_addr->sa_family,
                 tst_addr, iut_addr,
                 sock_type == RPC_SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP,
                 TRUE));

    rc = net_drv_rss_send_check_stats(tst_rpcs, tst_s, NULL,
                                      iut_rpcs, iut_s, NULL,
                                      sock_type, init_queue, bpf_id,
                                      multi_seg,
                                      "Before hash key change");
    if (rc != 0)
        test_failed = TRUE;

    for (i = 0; i < MAX_ATTEMPTS; i++)
    {
        for (j = 0; j < ctx.key_len; j++)
            new_key[j] = rand_range(0, 0xff);

        CHECK_RC(net_drv_rss_ctx_change_key(&ctx, new_key, ctx.key_len));

        CHECK_RC(net_drv_rss_predict(&ctx, tst_addr, iut_addr,
                                     &hash, &idx, &new_queue));
        if (new_queue != init_queue)
            break;
    }

    if (i == MAX_ATTEMPTS)
        TEST_FAIL("Failed to find a new hash resulting in Rx queue change");

    TEST_STEP("Set RSS hash key on IUT interface to a different value such "
              "that Toeplitz hash for packets sent from Tester now points "
              "to a different RSS indirection table entry which specifies "
              "a different Rx queue.");

    CHECK_RC(tapi_cfg_if_rss_hash_key_set_local(iut_rpcs->ta,
                                                iut_if->if_name, 0,
                                                new_key, ctx.key_len));
    CHECK_RC(tapi_cfg_if_rss_hash_indir_commit(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    TEST_STEP("Send again a few packets from Tester. "
              "Check that XDP hook reports that all the packets were "
              "processed by the new Rx queue.");

    RING("With changed hash key hash value should be 0x%x and packets "
         "should go via queue %u specified in indirection table entry %u",
         hash, new_queue, idx);

    CHECK_RC(net_drv_rss_send_check_stats(tst_rpcs, tst_s, NULL,
                                          iut_rpcs, iut_s, NULL,
                                          sock_type, new_queue, bpf_id,
                                          multi_seg,
                                          "After hash key change"));

    if (test_failed)
        TEST_STOP;
    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    free(new_key);

    net_drv_rss_ctx_release(&ctx);

    TEST_END;
}
