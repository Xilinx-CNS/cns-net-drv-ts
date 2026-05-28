/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-rx_rule_tcp_udp Adding Rx rule for TCP or UDP
 * @ingroup rss
 * @{
 *
 * @objective Check that Rx classification rule can redirect matching
 *            TCP/UDP traffic to a different Rx queue.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param sock_type      Socket type:
 *                       - @c SOCK_STREAM
 *                       - @c SOCK_DGRAM
 * @param match_src      If @c FALSE, source address/port should be omitted
 *                       in Rx rule
 * @param match_dst      If @c FALSE, destination address/port should be
 *                       omitted in Rx rule
 * @param location       Location for the added rule:
 *                       - @c first
 *                       - @c last
 *                       - @c any
 *                       - @c specific (request specific place in rules
 *                         table)
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "rss/rx_rule_tcp_udp"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "common_rss.h"
#include "tapi_cfg_rx_rule.h"

/* Maximum number of loop iterations before giving up */
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
    te_bool match_src;
    te_bool match_dst;
    int64_t location;

    struct sockaddr_storage new_iut_addr_st;
    struct sockaddr_storage new_tst_addr_st;
    struct sockaddr *new_iut_addr = NULL;
    struct sockaddr *new_tst_addr = NULL;
    uint16_t iut_port;
    uint16_t tst_port;
    uint16_t new_iut_port;
    uint16_t new_tst_port;
    unsigned int i;

    unsigned int bpf_id;
    unsigned int init_queue;
    unsigned int new_queue;
    unsigned int cur_queue;

    uint32_t rules_table_size;
    tapi_cfg_rx_rule_flow flow_type;

    int iut_s1 = -1;
    int tst_s1 = -1;
    int iut_s2 = -1;
    int tst_s2 = -1;

    net_drv_rss_ctx rss_ctx = NET_DRV_RSS_CTX_INIT;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BOOL_PARAM(multi_seg);
    TEST_GET_SOCK_TYPE(sock_type);
    TEST_GET_BOOL_PARAM(match_src);
    TEST_GET_BOOL_PARAM(match_dst);
    TEST_GET_ENUM_PARAM(location, NET_DRV_RX_RULE_LOC);

    net_drv_rx_rules_check_table_size(iut_rpcs->ta, iut_if->if_name,
                                      &rules_table_size);

    if (location != 0)
    {
        net_drv_rx_rules_check_spec_loc(iut_rpcs->ta, iut_if->if_name);
    }
    else
    {
        CHECK_RC(tapi_cfg_rx_rule_find_location(iut_rpcs->ta,
                                                iut_if->if_name,
                                                0, rules_table_size,
                                                &location));
    }

    net_drv_rss_ctx_prepare(&rss_ctx, iut_rpcs->ta, iut_if->if_name, 0);

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    TEST_STEP("Create the first pair of connected sockets on IUT and "
              "Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s1, &tst_s1);

    TEST_STEP("Compute RSS hash and predict the Rx queue @b init_queue "
              "that should process packets send from Tester over the first "
              "connection.");
    CHECK_RC(net_drv_rss_predict(&rss_ctx, tst_addr, iut_addr,
                                 NULL, NULL, &init_queue));

    tapi_sockaddr_clone_exact(iut_addr, &new_iut_addr_st);
    tapi_sockaddr_clone_exact(tst_addr, &new_tst_addr_st);
    new_iut_addr = SA(&new_iut_addr_st);
    new_tst_addr = SA(&new_tst_addr_st);

    TEST_STEP("Find another pair of ports on IUT and Tester such that "
              "the same Rx queue will be used to receive packets "
              "if sockets are bound to these new ports.");

    iut_port = te_sockaddr_get_port(iut_addr);
    tst_port = te_sockaddr_get_port(tst_addr);

    for (i = 0; i < MAX_ATTEMPTS; i++)
    {
        new_iut_port = tapi_get_random_port();
        new_tst_port = tapi_get_random_port();
        if (new_iut_port == iut_port || new_tst_port == tst_port)
            continue;

        te_sockaddr_set_port(new_iut_addr, htons(new_iut_port));
        te_sockaddr_set_port(new_tst_addr, htons(new_tst_port));

        CHECK_RC(net_drv_rss_predict(&rss_ctx, new_tst_addr, new_iut_addr,
                                     NULL, NULL, &cur_queue));

        if (cur_queue == init_queue)
        {
            if (rpc_check_port_is_free(iut_rpcs, new_iut_port) &&
                rpc_check_port_is_free(tst_rpcs, new_tst_port))
            {
                break;
            }
        }
    }

    if (i == MAX_ATTEMPTS)
        TEST_FAIL("Cannot find ports for the second connection");

    TEST_STEP("Create the second pair of connected sockets on IUT "
              "and Tester bound to these new ports.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   new_iut_addr, new_tst_addr, &iut_s2, &tst_s2);

    RING("Packets should be received by Rx queue %u for both connections",
         init_queue);

    TEST_STEP("Send packets from Tester to IUT via the first connection, "
              "check that on IUT they are received by @b init_queue Rx "
              "queue.");
    CHECK_RC(net_drv_rss_send_check_stats(
                                   tst_rpcs, tst_s1, tst_addr,
                                   iut_rpcs, iut_s1, iut_addr,
                                   sock_type, init_queue, bpf_id, multi_seg,
                                   "Sending data via the first connection "
                                   "before adding a rule"));

    TEST_STEP("Send packets from Tester to IUT via the second connection, "
              "check that on IUT they are received by @b init_queue Rx "
              "queue.");
    CHECK_RC(net_drv_rss_send_check_stats(
                                   tst_rpcs, tst_s2, new_tst_addr,
                                   iut_rpcs, iut_s2, new_iut_addr,
                                   sock_type, init_queue, bpf_id, multi_seg,
                                   "Sending data via the second connection "
                                   "before adding a rule"));

    for (i = 0; i < MAX_ATTEMPTS; i++)
    {
        new_queue = rand_range(0, rss_ctx.rx_queues - 1);
        if (new_queue != init_queue)
            break;
    }

    if (i == MAX_ATTEMPTS)
        TEST_FAIL("Cannot select new Rx queue");

    TEST_STEP("Add Rx classification rule directing packets received via "
              "the first connection to another Rx queue @b new_queue.");

    flow_type = tapi_cfg_rx_rule_flow_by_socket(iut_addr->sa_family,
                                                sock_type);

    CHECK_RC(tapi_cfg_rx_rule_add(iut_rpcs->ta, iut_if->if_name,
                                  location, flow_type));

    CHECK_RC(tapi_cfg_rx_rule_fill_ip_addrs_ports(
                                       iut_rpcs->ta, iut_if->if_name,
                                       location,
                                       iut_addr->sa_family,
                                       match_src ? tst_addr : NULL, NULL,
                                       match_dst ? iut_addr : NULL, NULL));

    CHECK_RC(tapi_cfg_rx_rule_rx_queue_set(iut_rpcs->ta, iut_if->if_name,
                                           location,
                                           new_queue));

    rc = tapi_cfg_rx_rule_commit(iut_rpcs->ta, iut_if->if_name,
                                 location);
    if (rc != 0)
        TEST_VERDICT("Failed to add Rx rule: %r", rc);

    TEST_STEP("Send packets from Tester to IUT via the first connection, "
              "check that on IUT they are received by @b new_queue Rx "
              "queue.");
    RING("Packets should be received by Rx queue %u for the first "
         "connection", new_queue);
    CHECK_RC(net_drv_rss_send_check_stats(
                                   tst_rpcs, tst_s1, tst_addr,
                                   iut_rpcs, iut_s1, iut_addr,
                                   sock_type, new_queue, bpf_id, multi_seg,
                                   "Sending data via the first connection "
                                   "after adding a rule"));

    TEST_STEP("Send packets from Tester to IUT via the second connection, "
              "check that on IUT they are received by @b init_queue Rx "
              "queue.");
    RING("Packets should be received by Rx queue %u for the second "
         "connection", init_queue);
    CHECK_RC(net_drv_rss_send_check_stats(
                                   tst_rpcs, tst_s2, new_tst_addr,
                                   iut_rpcs, iut_s2, new_iut_addr,
                                   sock_type, init_queue, bpf_id, multi_seg,
                                   "Sending data via the second connection "
                                   "after adding a rule"));

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s1);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s1);
    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s2);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s2);

    if (rss_ctx.rx_queues > 0)
    {
        CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));
        net_drv_rss_ctx_release(&rss_ctx);
    }

    TEST_END;
}
