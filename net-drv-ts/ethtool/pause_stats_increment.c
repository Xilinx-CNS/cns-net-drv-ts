/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-pause_stats_increment Check pause statistics increment
 * @ingroup ethtool
 * @{
 *
 * @objective Generate pause frames and check that Rx pause frames
 *            counter on IUT interface increases.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *
 * @author Denis Pryazhennikov <denis.pryazhennikov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "ethtool/pause_stats_increment"

#include "net_drv_test.h"
#include "tapi_cfg_if_flow_control.h"
#include "tapi_ethtool.h"
#include "tapi_job_factory_rpc.h"
#include "tapi_tad.h"
#include "tapi_eth.h"

#include <limits.h>

/* Number of pause frames to send. */
#define PAUSE_FRAMES_NUM 64
/* Pause timer value in generated pause frames. */
#define PAUSE_QUANTA 0x0001

static int
get_rx_pause_frames(tapi_job_factory_t *factory, const char *if_name,
                    const char *stage)
{
    tapi_ethtool_report report = tapi_ethtool_default_report;
    tapi_ethtool_opt opts = tapi_ethtool_default_opt;
    int value;
    te_errno rc;

    opts.cmd = TAPI_ETHTOOL_CMD_SHOW_PAUSE;
    opts.stats = true;
    opts.if_name = if_name;

    rc = tapi_ethtool(factory, &opts, &report);
    if (rc != 0)
    {
        if (report.err_code == TE_EOPNOTSUPP)
            TEST_SKIP("Ethtool command is not supported");

        TEST_VERDICT("%s: failed to process ethtool command, rc=%r",
                     stage, rc);
    }

    if (report.err_out)
    {
        ERROR_VERDICT("%s: ethtool printed something to stderr", stage);
    }

    if (!report.data.pause.rx)
        TEST_SKIP("Reception of pause frames is disabled");

    if (!report.data.pause.rx_pause_frames.defined)
        TEST_SKIP("Rx pause frames counter is not available");

    if (report.data.pause.rx_pause_frames.value > INT_MAX)
    {
        TEST_VERDICT("%s: Rx pause frames counter value is too big",
                     stage);
    }

    value = (int)report.data.pause.rx_pause_frames.value;

    tapi_ethtool_destroy_report(&report);

    return value;
}

static void
enable_rx_pause(const char *ta, const char *if_name)
{
    tapi_cfg_if_fc params = TAPI_CFG_IF_FC_INIT;
    te_errno rc;
    int err;

    params.rx = 1;

    rc = tapi_cfg_if_fc_set(ta, if_name, &params);
    err = TE_RC_GET_ERROR(rc);
    if (err == TE_ENOENT || err == TE_EOPNOTSUPP)
    {
       TEST_SKIP("Interface flow control parameters are not supported");
    }
    else if (err != 0)
    {
       TEST_VERDICT("Failed to enable Rx pause handling on %s, rc=%r",
                    if_name, rc);
    }
    CFG_WAIT_CHANGES;
    net_drv_wait_up(ta, if_name);
}

static void
send_pause_frames(const char *ta, const char *if_name, const uint8_t *src_addr)
{
    csap_handle_t csap = CSAP_INVALID_HANDLE;
    const uint16_t pause_time = PAUSE_QUANTA;
    asn_value *tmpl = NULL;

    CHECK_RC(tapi_eth_csap_create(ta, 0, if_name, TAD_ETH_RECV_NO,
                                  NULL, NULL, NULL, &csap));
    CHECK_RC(tapi_eth_add_pause_pdu(&tmpl, NULL, false, NULL, src_addr,
                                    &pause_time, TE_BOOL3_FALSE));
    CHECK_RC(tapi_tad_add_iterator_for(tmpl, 1, PAUSE_FRAMES_NUM, 1));
    CHECK_RC(tapi_tad_trsend_start(ta, 0, csap, tmpl, RCF_MODE_BLOCKING));

    asn_free_value(tmpl);
    CHECK_RC(tapi_tad_csap_destroy(ta, 0, csap));
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;
    const struct sockaddr *tst_lladdr = NULL;

    tapi_job_factory_t *factory = NULL;

    int rx_pause_frames_before;
    int rx_pause_frames_after;
    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_LINK_ADDR(tst_lladdr);

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));

    TEST_STEP("Enable Rx pause handling on IUT.");
    enable_rx_pause(iut_rpcs->ta, iut_if->if_name);

    TEST_STEP("Save the current value of Rx pause frames counter on IUT.");
    rx_pause_frames_before = get_rx_pause_frames(
                                 factory, iut_if->if_name,
                                 "before transmitting pause frames");

    TEST_STEP("Send pause frames from Tester to IUT.");
    send_pause_frames(tst_rpcs->ta, tst_if->if_name,
                      (const uint8_t *)tst_lladdr->sa_data);

    TEST_STEP("Wait for a while to let pause statistics update.");
    NET_DRV_WAIT_IF_STATS_UPDATE;

    TEST_STEP("Obtain a new value of Rx pause frames counter on IUT.");
    rx_pause_frames_after = get_rx_pause_frames(factory, iut_if->if_name,
                                                "after transmitting pause frames");

    if (rx_pause_frames_after < rx_pause_frames_before)
    {
        TEST_VERDICT("Rx pause frames counter decreased from %d to %d",
                     rx_pause_frames_before, rx_pause_frames_after);
    }

    RING("Rx pause frames before: %d, after: %d",
         rx_pause_frames_before, rx_pause_frames_after);

    if (rx_pause_frames_after == rx_pause_frames_before)
    {
        TEST_VERDICT("Rx pause frames counter did not increase");
    }

    TEST_SUCCESS;

cleanup:

    tapi_job_factory_destroy(factory);

    TEST_END;
}
