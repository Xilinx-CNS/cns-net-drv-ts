/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Offloads tests
 */

/**
 * @defgroup offload-simple_csum Turning on/off Rx/Tx checksum offloading.
 * @ingroup offload
 * @{
 *
 * @objective Check that data can be sent or received after changing
 *            checksum offloading settings.
 *
 * @param env           Testing environment.
 *                      - @ref env-peer2peer
 *                      - @ref env-peer2peer_ipv6
 * @param sock_type     Socket type:
 *                      - @ref sock_stream_dgram
 * @param rx_csum       If @c TRUE, Rx checksum offloading is checked;
 *                      otherwise Tx is checked.
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "offload/simple_csum"

#include "net_drv_test.h"
#include "tapi_cfg_if.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    rpc_socket_type sock_type;
    te_bool rx_csum;

    const char *feature_name = NULL;
    int init_val;

    int iut_s = -1;
    int tst_s = -1;

    rcf_rpc_server *rpcs_sender = NULL;
    rcf_rpc_server *rpcs_receiver = NULL;
    int s_sender = -1;
    int s_receiver = -1;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_SOCK_TYPE(sock_type);
    TEST_GET_BOOL_PARAM(rx_csum);

    if (rx_csum)
    {
        feature_name = "rx-checksum";
    }
    else
    {
        if (iut_addr->sa_family == AF_INET)
            feature_name = "tx-checksum-ipv4";
        else
            feature_name = "tx-checksum-ipv6";

        /*
         * If protocol-specific checksum is not supported,
         * try generic Tx IP checksum instead.
         */
        if (!net_drv_req_if_feature_configurable(iut_rpcs->ta,
                                                 iut_if->if_name,
                                                 feature_name))
            feature_name = "tx-checksum-ip-generic";
    }

    net_drv_req_if_feature_change(iut_rpcs->ta,
                                  iut_if->if_name,
                                  feature_name);

    TEST_STEP("Establish connection between a pair of sockets of type "
              "@p sock_type on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    if (rx_csum)
    {
        rpcs_sender = tst_rpcs;
        rpcs_receiver = iut_rpcs;
        s_sender = tst_s;
        s_receiver = iut_s;
    }
    else
    {
        rpcs_sender = iut_rpcs;
        rpcs_receiver = tst_rpcs;
        s_sender = iut_s;
        s_receiver = tst_s;
    }

    TEST_STEP("Make sure that the tested checksum offloading is enabled.");
    CHECK_RC(tapi_cfg_if_feature_get(iut_rpcs->ta, iut_if->if_name,
                                     feature_name, &init_val));
    RING("Initial value of %s on the IUT interface %s is %d",
         feature_name, iut_if->if_name, init_val);

    if (init_val == 0)
    {
        rc = tapi_cfg_if_feature_set(iut_rpcs->ta,
                                     iut_if->if_name,
                                     feature_name, 1);
        if (rc != 0)
        {
            TEST_VERDICT("Failed to enable %s on IUT, rc=%r",
                         feature_name, rc);
        }

        CFG_WAIT_CHANGES;
    }

    TEST_STEP("If @p rx_csum is @c TRUE, send some data from Tester to "
              "IUT, otherwise - from IUT to Tester. Receive and check "
              "the sent data.");
    net_drv_send_recv_check(rpcs_sender, s_sender,
                            rpcs_receiver, s_receiver,
                            "Sending when checksum offload is enabled");

    TEST_STEP("Disable the tested checksum offloading.");
    rc = tapi_cfg_if_feature_set(iut_rpcs->ta,
                                 iut_if->if_name,
                                 feature_name, 0);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to disable %s on IUT, rc=%r",
                     feature_name, rc);
    }

    CFG_WAIT_CHANGES;

    TEST_STEP("Repeat the step with sending/receiving/checking data.");
    net_drv_send_recv_check(rpcs_sender, s_sender,
                            rpcs_receiver, s_receiver,
                            "Sending when checksum offload is disabled");

    TEST_STEP("Re-enable the tested checksum offloading.");
    rc = tapi_cfg_if_feature_set(iut_rpcs->ta,
                                 iut_if->if_name,
                                 feature_name, 1);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to re-enable %s on IUT, rc=%r",
                     feature_name, rc);
    }

    CFG_WAIT_CHANGES;

    TEST_STEP("Repeat the step with sending/receiving/checking data.");
    net_drv_send_recv_check(rpcs_sender, s_sender,
                            rpcs_receiver, s_receiver,
                            "Sending when checksum offload is re-enabled");

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    TEST_END;
}

/** @} */
