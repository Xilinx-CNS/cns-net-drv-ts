/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-statistics Getting interface statistics with ethtool
 * @ingroup ethtool
 * @{
 *
 * @objective Check that ethtool shows interface statistics.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param sock_type      Socket type:
 *                       - @c SOCK_STREAM
 *                       - @c SOCK_DGRAM
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ET003.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/statistics"

#include "net_drv_test.h"
#include "tapi_ethtool.h"
#include "tapi_job_factory_rpc.h"
#include "te_str.h"
#include "te_string.h"

/*
 * Statistic names are NIC/driver specific, so multiple
 * names are tried here for each statistic in hope that
 * one of them is used.
 */

/* Check whether end of the string matches pattern */
#define strcmp_end(_pattern, _str) \
    strcmp(_str + strlen(_str) - sizeof(_pattern) + 1, \
           _pattern)

/*
 * Get either the value of single matching statistic or the sum of
 * multiple matching statistics.
 */
static long unsigned int
get_stats(tapi_ethtool_report *report, te_bool tx)
{
    te_kvpair *kv = NULL;
    long unsigned int val;
    long unsigned int total = 0;

    te_string str = TE_STRING_INIT;

    TAILQ_FOREACH(kv, &report->data.stats, links)
    {
        if ((tx &&
             (strcmp(kv->key, "tx_bytes") == 0 ||
              strcmp(kv->key, "tx_octets") == 0 ||
              strcmp(kv->key, "port_tx_bytes") == 0 ||
              strcmp(kv->key, "port_tx_unicast") == 0)) ||
            (!tx &&
             (strcmp(kv->key, "rx_bytes") == 0 ||
              strcmp(kv->key, "rx_octets") == 0 ||
              strcmp(kv->key, "port_rx_bytes") == 0 ||
              strcmp(kv->key, "port_rx_unicast") == 0)))
        {
            /*
             * The single matching statistic is found: take it
             * and ignore the rest.
             */
            te_string_reset(&str);
            te_string_append(&str, "%s: %s\n", kv->key, kv->value);
            CHECK_RC(te_strtoul(kv->value, 10, &total));
            break;
        }

        if (((tx && strcmp_start("tx_queue_", kv->key) == 0) ||
             (!tx && strcmp_start("rx_queue_", kv->key) == 0)) &&
            strcmp_end("_bytes", kv->key) == 0)
        {
            te_string_append(&str, "%s: %s\n", kv->key, kv->value);
            CHECK_RC(te_strtoul(kv->value, 10, &val));
            total += val;
        }
    }

    RING("Available %sx statistics:\n%s\nTotal:%lu", (tx ? "T" : "R"),
         str.ptr, total);
    te_string_free(&str);

    return total;
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    long unsigned int tx_bytes1;
    long unsigned int rx_bytes1;
    long unsigned int tx_bytes2;
    long unsigned int rx_bytes2;

    rpc_socket_type sock_type;
    int iut_s = -1;
    int tst_s = -1;

    tapi_job_factory_t *factory = NULL;
    tapi_ethtool_report report = tapi_ethtool_default_report;
    tapi_ethtool_opt opts = tapi_ethtool_default_opt;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_SOCK_TYPE(sock_type);

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));

    TEST_STEP("Run '@b ethtool -S @p iut_if' on IUT. Check that it "
              "terminated with zero exit code and did not print anything "
              "to @b stderr.");

    opts.cmd = TAPI_ETHTOOL_CMD_STATS;
    opts.if_name = iut_if->if_name;

    rc = tapi_ethtool(factory, &opts, &report);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to process ethtool command before "
                     "transmitting data, rc=%r", rc);
    }

    if (report.err_out)
    {
        ERROR_VERDICT("ethtool printed something to stderr when run "
                      "before transmitting data");
    }

    TEST_STEP("Save current values of Tx and Rx bytes counters from "
              "@b ethtool output.");
    tx_bytes1 = get_stats(&report, TRUE);
    rx_bytes1 = get_stats(&report, FALSE);

    TEST_STEP("Create a pair of connected sockets of type @p sock_type on "
              "IUT and Tester, send some data in both directions between "
              "them, receive and check it.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    net_drv_conn_check(iut_rpcs, iut_s, "IUT",
                       tst_rpcs, tst_s, "Tester",
                       "Checking sending and receiving");

    TEST_STEP("Wait for a while to let statistics counters update.");
    NET_DRV_WAIT_IF_STATS_UPDATE;

    TEST_STEP("Again run '@b ethtool -S @p iut_if' on IUT. Check that it "
              "terminated with zero exit code and did not print anything "
              "to @b stderr.");

    rc = tapi_ethtool(factory, &opts, &report);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to process ethtool command after "
                     "transmitting data, rc=%r", rc);
    }

    if (report.err_out)
    {
        ERROR_VERDICT("ethtool printed something to stderr when run "
                      "after transmitting data");
    }

    TEST_STEP("Obtain new values of Tx and Rx bytes counters from "
              "@b ethtool output, check that they increased.");

    tx_bytes2 = get_stats(&report, TRUE);
    rx_bytes2 = get_stats(&report, FALSE);

    RING("Rx bytes change: %ld, Tx bytes change: %ld",
         rx_bytes2 - rx_bytes1,
         tx_bytes2 - tx_bytes1);

    if (tx_bytes2 <= tx_bytes1)
        ERROR_VERDICT("Tx bytes counter did not increase");

    if (rx_bytes2 <= rx_bytes1)
        ERROR_VERDICT("Rx bytes counter did not increase");

    if (tx_bytes2 <= tx_bytes1 || rx_bytes2 <= rx_bytes1)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    tapi_ethtool_destroy_report(&report);
    tapi_job_factory_destroy(factory);

    TEST_END;
}
