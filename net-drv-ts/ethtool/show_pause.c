/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-show_pause Show pause parameters with ethtool
 * @ingroup ethtool
 * @{
 *
 * @objective Check that ethtool can show pause parameters for
 *            a tested interface.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ET005.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ethtool/show_pause"

#include "net_drv_test.h"
#include "tapi_ethtool.h"
#include "tapi_job_factory_rpc.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    tapi_job_factory_t *factory = NULL;
    tapi_ethtool_opt opts = tapi_ethtool_default_opt;
    tapi_ethtool_report report = tapi_ethtool_default_report;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));

    TEST_STEP("Run 'ethtool \\--show-pause @p iut_if'. Check that it "
              "terminates successfully, does not print anything to "
              "stderr and its output can be parsed.");

    opts.cmd = TAPI_ETHTOOL_CMD_SHOW_PAUSE;
    opts.stats = true;
    opts.if_name = iut_if->if_name;

    rc = tapi_ethtool(factory, &opts, &report);
    if (rc != 0)
    {
        if (report.err_code == TE_EOPNOTSUPP)
            TEST_SKIP("Ethtool command is not supported");

        TEST_VERDICT("Failed to process ethtool command");
    }

    if (report.err_out)
        ERROR_VERDICT("ethtool printed something to stderr");

    /* This is not necessarily a bug, so test does not fail here */
    if (!report.data.pause.autoneg)
        RING_VERDICT("Pause autonegotiation is disabled");
    if (!report.data.pause.rx)
        RING_VERDICT("Reception of pause frames is disabled");
    if (!report.data.pause.tx)
        RING_VERDICT("Transmission of pause frames is disabled");
    if (!report.data.pause.rx_pause_frames.defined)
        RING_VERDICT("Rx pause frames counter is not available");
    if (!report.data.pause.tx_pause_frames.defined)
        RING_VERDICT("Tx pause frames counter is not available");

    TEST_SUCCESS;

cleanup:

    tapi_job_factory_destroy(factory);
    tapi_ethtool_destroy_report(&report);

    TEST_END;
}
