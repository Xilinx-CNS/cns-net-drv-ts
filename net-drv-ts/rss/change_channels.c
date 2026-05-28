/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-change_channels Changing number of combined channels.
 * @ingroup rss
 * @{
 *
 * @objective Change number of combined channels and check that RSS
 *            indirection table works as expected after that.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param sock_type      Socket type:
 *                       - @c SOCK_STREAM
 *                       - @c SOCK_DGRAM
 * @param two_power      If @c TRUE, new number of combined channels
 *                       should be a power of two.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "rss/change_channels"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_cfg_if_chan.h"
#include "tapi_mem.h"
#include "tapi_cfg_sys.h"
#include "common_rss.h"

/* Upper limit for loops trying to choose some value */
#define MAX_ATTEMPTS 10000
/* Number of connections to use to test RSS indirection table */
#define CHECK_CONNS_NUM 25

/* Check that RSS indirection table works as expected. */
static void
check_indir_table(rcf_rpc_server *iut_rpcs, rcf_rpc_server *tst_rpcs,
                  const struct sockaddr *iut_addr,
                  const struct sockaddr *tst_addr,
                  rpc_socket_type sock_type,
                  int *iut_s, int *tst_s,
                  net_drv_rss_ctx *ctx, unsigned int bpf_id,
                  bool multi_seg,
                  const char *vpref)
{
    struct sockaddr_storage new_iut_addr_st;
    struct sockaddr_storage new_tst_addr_st;
    struct sockaddr *new_iut_addr = NULL;
    struct sockaddr *new_tst_addr = NULL;
    uint16_t iut_port;
    uint16_t tst_port;
    unsigned int indir_idx;
    unsigned int exp_queue;
    unsigned int i;
    unsigned int j;

    tapi_sockaddr_clone_exact(iut_addr, &new_iut_addr_st);
    tapi_sockaddr_clone_exact(tst_addr, &new_tst_addr_st);
    new_iut_addr = SA(&new_iut_addr_st);
    new_tst_addr = SA(&new_tst_addr_st);

    for (i = 0; i < CHECK_CONNS_NUM; i++)
    {
        for (j = 0; j < MAX_ATTEMPTS; j++)
        {
            iut_port = tapi_get_random_port();
            tst_port = tapi_get_random_port();

            if (rpc_check_port_is_free(iut_rpcs, iut_port) &&
                rpc_check_port_is_free(tst_rpcs, tst_port))
                break;
        }

        if (j == MAX_ATTEMPTS)
            TEST_FAIL("Cannot get a pair of free ports");

        te_sockaddr_set_port(new_iut_addr, htons(iut_port));
        te_sockaddr_set_port(new_tst_addr, htons(tst_port));

        GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                       new_iut_addr, new_tst_addr, iut_s, tst_s);

        CHECK_RC(net_drv_rss_predict(ctx, new_tst_addr, new_iut_addr,
                                     NULL, &indir_idx, &exp_queue));

        RING("Expected indirection table entry %u, queue %u",
             indir_idx, exp_queue);

        CHECK_RC(net_drv_rss_send_check_stats(
                                      tst_rpcs, *tst_s, new_tst_addr,
                                      iut_rpcs, *iut_s, new_iut_addr,
                                      sock_type, exp_queue,
                                      bpf_id, multi_seg, vpref));

        RPC_CLOSE(iut_rpcs, *iut_s);
        RPC_CLOSE(tst_rpcs, *tst_s);
    }
}

/* Change records of indirection table */
static te_errno
change_indir_table(const char *ta, const char *if_name,
                   int *init_table, unsigned int table_size,
                   unsigned int min_rxq, unsigned int max_rxq)
{
    unsigned int i;
    unsigned int j;
    int new_rxq;

    for (i = 0; i < table_size; i++)
    {
        for (j = 0; j < MAX_ATTEMPTS; j++)
        {
            new_rxq = rand_range(min_rxq, max_rxq);
            if (new_rxq != init_table[i])
                break;
            if (min_rxq == max_rxq)
                break;
        }

        CHECK_RC(tapi_cfg_if_rss_indir_set_local(ta, if_name, 0,
                                                 i, new_rxq));
    }

    return tapi_cfg_if_rss_hash_indir_commit(ta, if_name, 0);
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    rpc_socket_type sock_type;
    te_bool two_power;
    bool multi_seg;

    int iut_s = -1;
    int tst_s = -1;

    unsigned int bpf_id = 0;

    int combined_max = -1;
    int combined_init = -1;
    int combined_new = -1;
    unsigned int i;
    unsigned int j;
    int rx_queue;
    int exp_queue;
    int *indir_table = NULL;

    te_bool unexp_table_verdict = FALSE;
    te_bool success = FALSE;
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
    TEST_GET_BOOL_PARAM(two_power);

    net_drv_rss_ctx_prepare(&ctx, iut_rpcs->ta, iut_if->if_name, 0);

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    TEST_STEP("Log initial state of RSS indirection table.");
    CHECK_RC(tapi_cfg_if_rss_print_indir_table(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    TEST_STEP("Reset RSS indirection table to default state. This is "
              "done to ensure that if the number of combined channels "
              "is reduced so that some Rx queues disappear, it "
              "will not result in failure.");
    CHECK_RC(tapi_cfg_if_rss_indir_default_set(iut_rpcs->ta,
                                               iut_if->if_name));

    if (iut_addr->sa_family == AF_INET6)
    {
        /*
         * Without this, IPv6 address disappears from SFC NIC
         * after changing number of channels.
         */
        TEST_SUBSTEP("If IPv6 is checked, enable @b keep_addr_on_down "
                     "setting for the IUT interface to avoid IPv6 "
                     "address disappearance after changing number "
                     "of channels.");
        CHECK_RC(tapi_cfg_sys_set_int(
                            iut_rpcs->ta, 1, NULL,
                            "net/ipv6/conf:%s/keep_addr_on_down",
                            iut_if->if_name));
    }

    TEST_STEP("Change number of combined channels for the IUT interface. "
              "If @p two_power is @c TRUE, the new number should be a "
              "power of two.");

    CHECK_RC(tapi_cfg_if_chan_max_get(iut_rpcs->ta, iut_if->if_name,
                                      TAPI_CFG_IF_CHAN_COMBINED,
                                      &combined_max));
    CHECK_RC(tapi_cfg_if_chan_cur_get(iut_rpcs->ta, iut_if->if_name,
                                      TAPI_CFG_IF_CHAN_COMBINED,
                                      &combined_init));

    if (combined_init < 1)
        TEST_SKIP("Initial number of combined channels is zero");

    if (combined_max < 3 || (two_power && combined_max < 4))
        TEST_SKIP("Cannot change number of combined channels");

    for (i = 0; i < MAX_ATTEMPTS; i++)
    {
        combined_new = rand_range(2, combined_max);

        if (combined_new == combined_init)
            continue;

        for (j = combined_new; j > 1 && j % 2 == 0; j /= 2);

        if ((j > 1 && two_power) || (j == 1 && !two_power))
            continue;

        success = TRUE;
        break;
    }

    if (!success)
        TEST_FAIL("Cannot choose a different number of combined channels");

    RING("Change the number of combined channels from %u to %u",
         combined_init, combined_new);
    CHECK_RC(tapi_cfg_if_chan_cur_set(iut_rpcs->ta, iut_if->if_name,
                                      TAPI_CFG_IF_CHAN_COMBINED, combined_new));
    CFG_WAIT_CHANGES;

    CHECK_RC(cfg_synchronize_fmt(TRUE, "/agent:%s/interface:%s",
                                 iut_rpcs->ta, iut_if->if_name));

    TEST_STEP("Log the new state of RSS indirection table.");
    CHECK_RC(tapi_cfg_if_rss_print_indir_table(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    indir_table = tapi_calloc(ctx.indir_table_size, sizeof(*indir_table));

    TEST_STEP("Check that now RSS indirection table is filled in an "
              "expected way: for every record N, Rx queue specified in "
              "it should be N modulo the number of combined channels.");
    for (i = 0; i < ctx.indir_table_size; i++)
    {
        CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta, iut_if->if_name, 0,
                                           i, &rx_queue));
        exp_queue = i % combined_new;
        if (rx_queue != exp_queue && !unexp_table_verdict)
        {
            RING("For entry %u expected queue is %d but %d is set instead",
                 i, exp_queue, rx_queue);
            RING_VERDICT("RSS indirection table is not filled in an "
                         "expected way after changing number of "
                         "combined channels");
            unexp_table_verdict = TRUE;
        }

        indir_table[i] = rx_queue;
    }

    TEST_STEP("Test RSS indirection table on IUT by repeating a few times "
              "the following steps:");
    TEST_SUBSTEP("Create a connected pair of sockets of type @p sock_type "
                 "on IUT and Tester, choosing ports randomly.");
    TEST_SUBSTEP("Send a few packets from Tester; find the Rx queue that "
                 "received them on IUT. Check that this Rx queue matches "
                 "the entry of RSS indirection table determined by "
                 "addresses and ports of connected sockets.");
    check_indir_table(iut_rpcs, tst_rpcs, iut_addr, tst_addr, sock_type,
                      &iut_s, &tst_s, &ctx, bpf_id, multi_seg,
                      "After changing number of combined channels");

    TEST_STEP("Change records of RSS indirection table on IUT.");

    rc = change_indir_table(iut_rpcs->ta, iut_if->if_name,
                            indir_table, ctx.indir_table_size,
                            0, combined_new - 1);
    if (rc != 0)
    {
        ERROR_VERDICT("Cannot change indirection table after changing "
                      "number of combined channels");
        test_failed = TRUE;

        if (combined_init < combined_new)
        {
            rc = change_indir_table(iut_rpcs->ta, iut_if->if_name,
                                    indir_table, ctx.indir_table_size,
                                    0, combined_init - 1);
            if (rc != 0)
            {
                TEST_VERDICT("Indirection table cannot be changed even "
                             "when only RX queues allowed by the initial "
                             "number of combined channels are used");
            }
            else
            {
                RING_VERDICT("Indirection table can be changed if "
                             "only Rx queues allowed by the initial "
                             "number of combined channels are used");
            }
        }
        else
        {
            TEST_STOP;
        }
    }

    CFG_WAIT_CHANGES;

    TEST_STEP("Again check RSS indirection table on IUT in the same "
              "way as before.");
    check_indir_table(iut_rpcs, tst_rpcs, iut_addr, tst_addr, sock_type,
                      &iut_s, &tst_s, &ctx, bpf_id, multi_seg,
                      "After changing indirection table entries");

    if (test_failed)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    net_drv_rss_ctx_release(&ctx);

    if (combined_init > 0)
    {
        CLEANUP_CHECK_RC(tapi_cfg_if_chan_cur_set(
                                          iut_rpcs->ta, iut_if->if_name,
                                          TAPI_CFG_IF_CHAN_COMBINED,
                                          combined_init));
        CFG_WAIT_CHANGES;
    }

    free(indir_table);

    TEST_END;
}
