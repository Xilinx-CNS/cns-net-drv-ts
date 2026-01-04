/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-rx_rules_full_part Two rules, one of them omits src or dst
 * @ingroup rss
 * @{
 *
 * @objective Check what happens when there are two TCP or UDP
 *            Rx rules - one specifying both source and destination
 *            and another one specifying only source or destination.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param sock_type      Socket type:
 *                       - @c SOCK_STREAM
 *                       - @c SOCK_DGRAM
 * @param first_partial  If @c TRUE, the first rule omits src or dst.
 *                       If @c FALSE, the second rule does that.
 * @param partial_src    If @c TRUE, one of the rules has only source
 *                       address/port specified. If @c FALSE - only
 *                       the destination address/port.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "rss/rx_rules_full_part"

#include "net_drv_test.h"
#include "te_toeplitz.h"
#include "tapi_cfg_if_rss.h"
#include "tapi_cfg_rx_rule.h"
#include "tapi_bpf_rxq_stats.h"
#include "common_rss.h"

/* Maximum number of loop iterations before giving up */
#define MAX_ATTEMPTS 10000

/* Add a new Rx rule */
static void
add_rule(const char *ta, const char *if_name,
         tapi_cfg_rx_rule_flow flow_type,
         const struct sockaddr *src_addr,
         const struct sockaddr *dst_addr,
         te_bool fill_src, te_bool fill_dst,
         unsigned int queue, const char *rule_name)
{
    te_errno rc;
    int64_t location;

    rc = net_drv_rx_rules_find_loc(ta, if_name, &location);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_CS, TE_ENOENT))
            TEST_SKIP("Rx rules are not supported");
        else
            TEST_VERDICT("Failed to find location for a new rule: %r", rc);
    }

    CHECK_RC(tapi_cfg_rx_rule_add(ta, if_name, location, flow_type));

    CHECK_RC(tapi_cfg_rx_rule_fill_ip_addrs_ports(
                                         ta, if_name, location,
                                         src_addr->sa_family,
                                         fill_src ? src_addr : NULL, NULL,
                                         fill_dst ? dst_addr : NULL, NULL));

    CHECK_RC(tapi_cfg_rx_rule_rx_queue_set(ta, if_name, location, queue));

    rc = tapi_cfg_rx_rule_commit(ta, if_name, location);
    if (rc != 0)
        TEST_VERDICT("Failed to add %s Rx rule: %r", rule_name, rc);
}

/* Check and free Rx queues statistics */
static void
check_free_stats(tapi_bpf_rxq_stats **stats, unsigned int stats_count,
                 unsigned int queue1, unsigned int queue2,
                 unsigned int def_queue, unsigned int exp_queue,
                 te_bool *failed, const char *vpref)
{
    unsigned int i;
    unsigned int got_queue;
    te_bool unknown_queue = FALSE;

    for (i = 0; i < stats_count; i++)
    {
        if ((*stats)[i].pkts == 0)
            continue;

        got_queue = (*stats)[i].rx_queue;

        if (got_queue != exp_queue)
        {
            if (got_queue == queue1)
            {
                ERROR_VERDICT("%s: the queue assigned to the first rule "
                              "received packets unexpectedly", vpref);
            }
            else if (got_queue == queue2)
            {
                ERROR_VERDICT("%s: the queue assigned to the second rule "
                              "received packets unexpectedly", vpref);
            }
            else if (got_queue == def_queue)
            {
                ERROR_VERDICT("%s: the queue defined by indirection table "
                              "received packets unexpectedly", vpref);
            }
            else if (!unknown_queue)
            {
                ERROR_VERDICT("%s: another Rx queue received packets "
                              "unexpectedly", vpref);
                unknown_queue = TRUE;
            }

            *failed = TRUE;
        }
    }

    free(*stats);
    *stats = NULL;
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
    te_bool first_partial;
    te_bool partial_src;

    unsigned int cur_queue;
    unsigned int def_queue;
    unsigned int queue1;
    unsigned int queue2;
    unsigned int bpf_id;
    tapi_cfg_rx_rule_flow flow_type;
    net_drv_rss_ctx rss_ctx = NET_DRV_RSS_CTX_INIT;

    int iut_s = -1;
    int tst_s = -1;

    te_bool first_src = TRUE;
    te_bool first_dst = TRUE;
    te_bool second_src = TRUE;
    te_bool second_dst = TRUE;

    struct sockaddr_storage new_iut_addr_st;
    struct sockaddr_storage new_tst_addr_st;
    struct sockaddr *new_iut_addr = NULL;
    struct sockaddr *new_tst_addr = NULL;
    int init_port;
    int new_port;
    tarpc_linger linger_opt;
    unsigned int i;

    tapi_bpf_rxq_stats *stats;
    unsigned int stats_count;
    te_bool test_failed = FALSE;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_SOCK_TYPE(sock_type);
    TEST_GET_BOOL_PARAM(first_partial);
    TEST_GET_BOOL_PARAM(partial_src);

    if (first_partial)
    {
        first_src = partial_src;
        first_dst = !partial_src;
    }
    else
    {
        second_src = partial_src;
        second_dst = !partial_src;
    }

    net_drv_rss_ctx_prepare(&rss_ctx, iut_rpcs->ta, iut_if->if_name, 0);

    if (rss_ctx.rx_queues < 3)
        TEST_SKIP("Less than 3 different Rx queues is available");

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    TEST_STEP("Determine @b def_queue which should be used for packets "
              "sent from @p tst_addr to @p iut_addr according to RSS "
              "indirection table.");
    CHECK_RC(net_drv_rss_predict(&rss_ctx, tst_addr, iut_addr,
                                 NULL, NULL, &def_queue));

    tapi_sockaddr_clone_exact(iut_addr, &new_iut_addr_st);
    tapi_sockaddr_clone_exact(tst_addr, &new_tst_addr_st);
    new_iut_addr = SA(&new_iut_addr_st);
    new_tst_addr = SA(&new_tst_addr_st);

    TEST_STEP("Find another port @b new_port for @p iut_addr (if "
              "@p partial_src is @c TRUE) or for @p tst_addr (if "
              "@p partial_src is @c FALSE) such that connection "
              "using this new port will still be mapped to "
              "@b def_queue according to RSS indirection table.");

    if (partial_src)
        init_port = te_sockaddr_get_port(iut_addr);
    else
        init_port = te_sockaddr_get_port(tst_addr);

    for (i = 0; i < MAX_ATTEMPTS; i++)
    {
        new_port = tapi_get_random_port();
        if (new_port == init_port)
            continue;

        if (partial_src)
            te_sockaddr_set_port(new_iut_addr, htons(new_port));
        else
            te_sockaddr_set_port(new_tst_addr, htons(new_port));

        CHECK_RC(net_drv_rss_predict(&rss_ctx, new_tst_addr, new_iut_addr,
                                     NULL, NULL, &cur_queue));

        if (cur_queue == def_queue)
        {
            if (partial_src)
            {
                if (!rpc_check_port_is_free(iut_rpcs, new_port))
                    continue;
            }
            else
            {
                if (!rpc_check_port_is_free(tst_rpcs, new_port))
                    continue;
            }

            break;
        }
    }

    if (i == MAX_ATTEMPTS)
        TEST_FAIL("Failed to pick up a port for the second connection");

    for (i = 0; i < MAX_ATTEMPTS; i++)
    {
        queue1 = rand_range(0, rss_ctx.rx_queues - 1);
        queue2 = rand_range(0, rss_ctx.rx_queues - 1);

        if (queue1 != queue2 && queue1 != def_queue && queue2 != def_queue)
            break;
    }

    if (i == MAX_ATTEMPTS)
        TEST_FAIL("Failed to pick up queues for Rx rules");

    TEST_STEP("Add two Rx classification rules on IUT. One of the rules "
              "should have both source (@p tst_addr) and destination "
              "(@p iut_addr) filled - let's call it the full rule. "
              "If @p first_partial is @c TRUE, it should be the second "
              "rule, otherwise - the first rule. Another rule should "
              "have only source (@p tst_addr) or destination (@p iut_addr) "
              "filled depending on @p partial_src test parameter - "
              "let's call it the partial rule. These two rules should "
              "specify two different Rx queues not equal to @b def_queue.");

    flow_type = tapi_cfg_rx_rule_flow_by_socket(iut_addr->sa_family,
                                                sock_type);

    add_rule(iut_rpcs->ta, iut_if->if_name, flow_type, tst_addr, iut_addr,
             first_src, first_dst, queue1, "the first");

    add_rule(iut_rpcs->ta, iut_if->if_name, flow_type, tst_addr, iut_addr,
             second_src, second_dst, queue2, "the second");

    RING("According to RSS table, packets should go via Rx queue %u.\n"
         "The first (%s) rule directs them to Rx queue %u.\n"
         "The second (%s) rule directs them to Rx queue %u.\n",
         def_queue,
         first_partial ? "partially specified" : "fully specified",
         queue1,
         !first_partial ? "partially specified" : "fully specified",
         queue2);

    TEST_STEP("Create the first connection of type @p sock_type using "
              "@p tst_addr on Tester and @p iut_addr on IUT.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    if (sock_type == RPC_SOCK_STREAM)
    {
        TEST_SUBSTEP("In case of TCP sockets, set @c SO_LINGER option "
                     "with zero linger time to ensure immediate release "
                     "of bound ports once sockets are closed.");
        linger_opt.l_onoff = 1;
        linger_opt.l_linger = 0;
        rpc_setsockopt(iut_rpcs, iut_s, RPC_SO_LINGER, &linger_opt);
        rpc_setsockopt(tst_rpcs, tst_s, RPC_SO_LINGER, &linger_opt);
    }

    TEST_STEP("Send a few packets from Tester over the first connection.");
    CHECK_RC(net_drv_rss_send_get_stats(
                                   tst_rpcs, tst_s, tst_addr,
                                   iut_rpcs, iut_s, iut_addr,
                                   sock_type, bpf_id,
                                   &stats, &stats_count,
                                   "The first connection"));

    TEST_STEP("Check that packets received on IUT went to the Rx queue "
              "assigned to the full rule.");
    check_free_stats(&stats, stats_count, queue1, queue2, def_queue,
                     first_partial ? queue2 : queue1,
                     &test_failed, "The first connection");

    TEST_STEP("Close sockets of the first connection.");
    RPC_CLOSE(iut_rpcs, iut_s);
    RPC_CLOSE(tst_rpcs, tst_s);

    TEST_STEP("Create the second connection using @p tst_addr on Tester "
              "and @p iut_addr on IUT. But this time bind to @b new_port "
              "on IUT (if @p partial_src is @c TRUE) or on Tester "
              "(if @p partial_src is @c FALSE). This will ensure that "
              "only the partial rule should match packets sent from "
              "Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   new_iut_addr, new_tst_addr, &iut_s, &tst_s);

    TEST_STEP("Send a few packets from Tester over the second connection.");
    CHECK_RC(net_drv_rss_send_get_stats(
                                   tst_rpcs, tst_s, new_tst_addr,
                                   iut_rpcs, iut_s, new_iut_addr,
                                   sock_type, bpf_id,
                                   &stats, &stats_count,
                                   "The second connection"));

    TEST_STEP("Check that packets received on IUT went to the Rx queue "
              "assigned to the partial rule.");
    check_free_stats(&stats, stats_count, queue1, queue2, def_queue,
                     first_partial ? queue1 : queue2,
                     &test_failed, "The second connection");

    if (test_failed)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    net_drv_rss_ctx_release(&rss_ctx);

    rc = tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id);
    if (rc != TE_RC(TE_CS, TE_ENOENT))
        CLEANUP_CHECK_RC(rc);

    TEST_END;
}
