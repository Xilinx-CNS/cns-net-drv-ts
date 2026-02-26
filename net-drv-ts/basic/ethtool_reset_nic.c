/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-ethtool_reset_nic Reset NIC with SIOCETHTOOL
 * @ingroup basic
 * @{
 *
 * @objective Check what happens when NIC is reset with @c SIOCETHTOOL.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *                      - @ref env-peer2peer_ipv6
 * @param flag          @c ETHTOOL_RESET flag to pass.
 * @param sock_type     Socket type:
 *                      - @c SOCK_STREAM
 *                      - @c SOCK_DGRAM
 * @param if_down       If @c TRUE, interface should be set DOWN before
 *                      resetting it.
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ST10, X3-ET019, X3-ET020.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "basic/ethtool_reset_nic"

#include "net_drv_test.h"
#include "te_ethtool.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_sys.h"
#include "tapi_cfg_phy.h"

/** How many milliseconds to wait for interface to be UP again */
#define MAX_IF_WAIT 160000

/** Maximum value of packets counter after reset */
#define MAX_PKTS_AFTER_RESET 50
/** Maximum value of bytes counter after reset */
#define MAX_BYTES_AFTER_RESET 5000

/** Minimum number of packets to send/receive before reset */
#define MIN_PKTS_BEFORE_RESET (MAX_PKTS_AFTER_RESET * 3)
/** Minimum number of bytes to send/receive before reset */
#define MIN_BYTES_BEFORE_RESET (MAX_BYTES_AFTER_RESET * 10)

/** Synchronize and print interface statistics */
static void
sync_print_if_stats(const char *ta, const char *if_name)
{
    NET_DRV_WAIT_IF_STATS_UPDATE;
    CHECK_RC(cfg_synchronize_fmt(TRUE, "/agent:%s/interface:%s/stats:",
                                 ta, if_name));
    CHECK_RC(cfg_tree_print(NULL, TE_LL_RING,
                            "/agent:%s/interface:%s/stats:",
                            ta, if_name));
}

static void
skip_with_restore(const char *ta, const char *if_name, bool if_up,
                  const char *message)
{
    if (if_up)
    {
        CHECK_RC(tapi_cfg_base_if_up(ta, if_name));
        net_drv_wait_up_gen(ta, if_name, false);
    }

    TEST_SKIP("%s", message);
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    int iut_s = -1;
    int tst_s = -1;

    cfg_handle *stats_hnd = NULL;
    unsigned int stats_num;
    unsigned int i;
    cfg_val_type vtype;
    uint64_t val;
    te_bool stats_zeroed;
    te_bool one_stat_zeroed;
    size_t sent;
    size_t received;

    int flag;
    unsigned int ret_flag;
    rpc_socket_type sock_type;
    te_bool if_down;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BIT_MASK_PARAM(flag, NET_DRV_RESET_FLAGS);
    TEST_GET_SOCK_TYPE(sock_type);
    TEST_GET_BOOL_PARAM(if_down);

    TEST_STEP("Establish connection between a pair of sockets of "
              "type @p sock_type on IUT and Tester.");

    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Check that data can be sent in both directions over the "
              "established connection. Send a lot of data so that "
              "it will be easier to tell whether counters are reset "
              "later.");

    iut_rpcs->silent_pass = iut_rpcs->silent_pass_default = TRUE;
    tst_rpcs->silent_pass = tst_rpcs->silent_pass_default = TRUE;
    for (i = 0, sent = 0, received = 0;
         i < MIN_PKTS_BEFORE_RESET || sent < MIN_BYTES_BEFORE_RESET ||
         received < MIN_BYTES_BEFORE_RESET;
         i++)
    {
        sent += net_drv_send_recv_check(
                                  iut_rpcs, iut_s, tst_rpcs, tst_s,
                                  "Sending data from IUT before reset");
        received += net_drv_send_recv_check(
                                  tst_rpcs, tst_s, iut_rpcs, iut_s,
                                  "Sending data from Tester before reset");
    }
    iut_rpcs->silent_pass = iut_rpcs->silent_pass_default = FALSE;
    tst_rpcs->silent_pass = tst_rpcs->silent_pass_default = FALSE;

    RING("%" TE_PRINTF_SIZE_T "u bytes were sent from IUT, "
         "%" TE_PRINTF_SIZE_T "u bytes were sent from Tester in "
         "%u packets", sent, received, i);

    sync_print_if_stats(iut_rpcs->ta, iut_if->if_name);

    if (iut_addr->sa_family == AF_INET6)
    {
        TEST_STEP("If IPv6 is checked, enable @b keep_addr_on_down "
                  "setting on the IUT interface to avoid network address "
                  "disappearance when interface is reset.");
        CHECK_RC(tapi_cfg_sys_set_int(
                            iut_rpcs->ta, 1, NULL,
                            "net/ipv6/conf:%s/keep_addr_on_down",
                            iut_if->if_name));
    }

    if (if_down)
    {
        TEST_STEP("If @p if_down is @c TRUE, set the IUT interface DOWN.");
        CHECK_RC(tapi_cfg_base_if_down(iut_rpcs->ta, iut_if->if_name));
    }

    CFG_WAIT_CHANGES;

    TEST_STEP("Call ioctl(@c SIOCETHTOOL / @c ETHTOOL_RESET) with reset "
              "flags set according to @p flag on the IUT interface.");

    RPC_AWAIT_ERROR(iut_rpcs);
    rc = net_drv_ethtool_reset(iut_rpcs, iut_s, iut_if->if_name,
                               flag, &ret_flag);
    if (flag == 0)
    {
        if (rc >= 0)
        {
            ERROR_VERDICT("ioctl() returned success after passing "
                          "no reset flags");
        }
        else if (RPC_ERRNO(iut_rpcs) == RPC_EOPNOTSUPP)
        {
            skip_with_restore(iut_rpcs->ta, iut_if->if_name, if_down,
                              "ETHTOOL_RESET is not supported");
        }
        else if (RPC_ERRNO(iut_rpcs) != RPC_EINVAL)
        {
            ERROR_VERDICT("ioctl() failed with unexpected errno %r",
                          RPC_ERRNO(iut_rpcs));
        }
    }
    else if (rc < 0)
    {
        if (RPC_ERRNO(iut_rpcs) == RPC_EOPNOTSUPP)
        {
            skip_with_restore(iut_rpcs->ta, iut_if->if_name, if_down,
                              "ETHTOOL_RESET is not supported");
        }
        else
        {
            TEST_VERDICT("ETHTOOL_RESET command failed with errno %r",
                         RPC_ERRNO(iut_rpcs));
        }
    }

    if (ret_flag != 0)
        WARN("Some reset flags were not cleared");

    TEST_STEP("Check that interface statistics is zeroed now "
              "(unless @p flag is @c none and interface was not "
              "set DOWN).");

    sync_print_if_stats(iut_rpcs->ta, iut_if->if_name);

    CHECK_RC(cfg_find_pattern_fmt(&stats_num, &stats_hnd,
                                  "/agent:%s/interface:%s/stats:/*:*",
                                  iut_rpcs->ta, iut_if->if_name));

    stats_zeroed = TRUE;
    one_stat_zeroed = TRUE;
    for (i = 0; i < stats_num; i++)
    {
        vtype = CVT_UINT64;
        CHECK_RC(cfg_get_instance(stats_hnd[i], &vtype, &val));

        /*
         * Some packets may be sent automatically soon after interface
         * is up again (neighbor discovery, etc), so here the code only
         * checks whether counters are not much bigger than zero.
         */
        if (val != 0)
        {
            char *subid;
            cfg_handle object;

            CHECK_RC(cfg_find_object_by_instance(stats_hnd[i], &object));
            CHECK_RC(cfg_get_subid(object, &subid));

            if (strstr(subid, "octets") != NULL)
            {
                if (val > MAX_BYTES_AFTER_RESET)
                    one_stat_zeroed = FALSE;
            }
            else
            {
                if (val > MAX_PKTS_AFTER_RESET)
                    one_stat_zeroed = FALSE;
            }

            if (!one_stat_zeroed)
            {
                if (flag)
                    ERROR_VERDICT("Interface statistic %s was not zeroed",
                                  subid);

                stats_zeroed = FALSE;
                one_stat_zeroed = TRUE;
            }

            free(subid);
        }
    }

    if (stats_zeroed && !if_down && !flag)
    {
        ERROR_VERDICT("Interface statistics was zeroed after "
                      "ETHTOOL_RESET command with zero flags");
    }

    if (if_down)
    {
        TEST_STEP("If @p if_down is @c TRUE, bring the IUT interface UP.");
        CHECK_RC(tapi_cfg_base_if_up(iut_rpcs->ta, iut_if->if_name));
        CFG_WAIT_CHANGES;
    }

    TEST_STEP("Wait until link status for the IUT interface is reported to "
              "be UP in configuration tree.");
    rc = tapi_cfg_phy_state_wait_up(iut_rpcs->ta, iut_if->if_name,
                                    MAX_IF_WAIT);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to wait until interface becomes UP "
                     "after reset, rc = %r", rc);
    }

    TEST_STEP("Check again that data can be sent and received in both "
              "directions between IUT and Tester sockets.");
    net_drv_conn_check(iut_rpcs, iut_s, "IUT", tst_rpcs, tst_s, "Tester",
                       "Checking after reset");

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    free(stats_hnd);

    TEST_END;
}
