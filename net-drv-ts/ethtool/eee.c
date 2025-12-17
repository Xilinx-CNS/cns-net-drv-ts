/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2025 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-eee Check Energy Efficient Ethernet
 * @ingroup ethtool
 * @{
 *
 * @objective Check that Energy Efficient Ethernet configuration works
 *            as expected.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/eee"

#include <math.h>

#include "net_drv_test.h"
#include "tapi_cfg_if_eee.h"
#include "tapi_job_factory_rpc.h"
#include "tapi_ping.h"

/*
 * Time in seconds to wait after changing EEE settings to be sure that
 * EEE was renegotiated.
 */
#define EEE_NEG_TIMEOUT 7

/* How long to send ping packets, in seconds */
#define PING_TIME 3
/* How long to wait for ping tool termination, in milliseconds */
#define PING_TIMEOUT TE_SEC2MS(PING_TIME + 2)
/* Delay between sending ping packets, in seconds */
#define PING_INTERVAL 0.01
/* Small LPI timer value, in microseconds */
#define LPI_TIMER_LOW 10
/* Big LPI timer value, in microseconds */
#define LPI_TIMER_HIGH 30000
/* Relative threshhold for comparing RTTs */
#define CMP_THRESHOLD 0.05

static rcf_rpc_server *iut_rpcs = NULL;
static rcf_rpc_server *tst_rpcs = NULL;
static const struct if_nameindex *iut_if = NULL;
static const struct if_nameindex *tst_if = NULL;
static const struct sockaddr *iut_addr = NULL;
static const struct sockaddr *tst_addr = NULL;

static int iut_s = -1;
static int tst_s = -1;

/*
 * Check that enabling or disabling EEE on IUT results in corresponding
 * change in EEE active state on Tester.
 */
static bool
check_eee_enable(bool enable)
{
    bool result = true;
    te_errno te_rc;
    bool active;
    const char *enabling_str = enable ? "enabling": "disabling";
    te_string vpref_str = TE_STRING_INIT_STATIC(100);

    te_string_append(&vpref_str, "Checking connection when EEE is %s",
                     (enable ? "enabled": "disabled"));

    te_rc = tapi_cfg_if_eee_enabled_set(iut_rpcs->ta, iut_if->if_name,
                                        enable);
    if (te_rc != 0)
    {
        TEST_VERDICT("Failed to %s EEE on IUT",
                     enable ? "enable" : "disable");
    }

    te_motivated_sleep(EEE_NEG_TIMEOUT, "Waiting for EEE negotiation");

    CHECK_RC(tapi_cfg_if_eee_active_get(iut_rpcs->ta, iut_if->if_name,
                                        &active));
    if (active != enable)
    {
        ERROR_VERDICT("After %s EEE, it is%s active on IUT", enabling_str,
                      active ? "": " not");
        result = false;
    }

    CHECK_RC(tapi_cfg_if_eee_active_get(tst_rpcs->ta, tst_if->if_name,
                                        &active));
    if (active != enable)
    {
        ERROR_VERDICT("After %s EEE, it is%s active on Tester", enabling_str,
                      active ? "": " not");
        result = false;
    }

    net_drv_conn_check(iut_rpcs, iut_s, "IUT socket",
                       tst_rpcs, tst_s, "Tester socket",
                       te_string_value(&vpref_str));

    return result;
}

/* Configure TX LPI on IUT and obtain average ping time. */
static double
check_lpi_ping(tapi_ping_app *ping_app, bool lpi_enable, int lpi_timer,
               const char *vpref)
{
    te_errno te_rc;
    tapi_ping_report report;
    double avg;

    CHECK_RC(tapi_cfg_if_eee_enabled_set_local(iut_rpcs->ta,
                                               iut_if->if_name,
                                               true));
    CHECK_RC(tapi_cfg_if_eee_tx_lpi_enabled_set_local(iut_rpcs->ta,
                                                      iut_if->if_name,
                                                      lpi_enable));
    if (lpi_timer >= 0)
    {
        CHECK_RC(tapi_cfg_if_eee_tx_lpi_timer_set_local(iut_rpcs->ta,
                                                        iut_if->if_name,
                                                        lpi_timer));
    }

    te_rc = tapi_cfg_if_eee_commit(iut_rpcs->ta, iut_if->if_name);
    if (te_rc != 0)
        TEST_VERDICT("%s: failed to change EEE state", vpref);

    te_motivated_sleep(EEE_NEG_TIMEOUT, "Waiting for EEE negotiation");

    /*
     * Recheking connection helps to ensure that ping is started on
     * a ready interface, so that there is no big initial delay.
     */
    net_drv_conn_check(iut_rpcs, iut_s, "IUT socket",
                       tst_rpcs, tst_s, "Tester socket",
                       vpref);

    CHECK_RC(tapi_ping_start(ping_app));
    CHECK_RC(tapi_ping_wait(ping_app, PING_TIMEOUT));
    CHECK_RC(tapi_ping_get_report(ping_app, &report));

    avg = report.rtt.avg;
    RING("%s: average ping time %f", vpref, avg);
    return avg;
}

/*
 * Compare two double values, returning 0 if they
 * are approximately equal.
 */
static int
approx_cmp(double v1, double v2)
{
    double r_diff;

    if (v1 == 0 && v2 == 0)
        return 0;

    r_diff = (v1 - v2) / MAX(v1, v2);
    if (fabs(r_diff) < CMP_THRESHOLD)
        return 0;
    else if (r_diff < 0)
        return -1;
    else
        return 1;
}

int
main(int argc, char *argv[])
{
    te_errno te_rc;
    bool active;
    bool success = true;

    tapi_ping_opt ping_opts = tapi_ping_default_opt;
    tapi_job_factory_t *factory = NULL;
    tapi_ping_app *ping_app = NULL;
    char *dst_addr = NULL;

    double ping_lpi_disabled;
    double ping_lpi_low;
    double ping_lpi_high;
    int cmp_r;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);

    TEST_STEP("Check initial EEE active state on Tester.");
    te_rc = tapi_cfg_if_eee_active_get(tst_rpcs->ta, tst_if->if_name,
                                       &active);
    if (te_rc != 0)
    {
        if (TE_RC_GET_ERROR(te_rc) == TE_ENOENT)
            TEST_SKIP("EEE is not supported");
        else
            TEST_VERDICT("Failed to get EEE active state: %r", rc);
    }

    RING("EEE initial state on Tester is%s active",
         active ? "" : " not");

    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Make sure that EEE is enabled on Tester.");
    te_rc = tapi_cfg_if_eee_enabled_set(tst_rpcs->ta, tst_if->if_name,
                                        true);
    if (te_rc != 0)
        TEST_VERDICT("Failed to enable EEE on Tester");

    TEST_STEP("Check that connection over the IUT interface works fine "
              "when EEE is enabled or disabled on IUT and EEE active state "
              "on Tester is set accordingly.");
    success = success && check_eee_enable(!active);
    success = success && check_eee_enable(active);
    if (!success)
        TEST_STOP;

    CHECK_RC(te_sockaddr_h2str(tst_addr, &dst_addr));
    ping_opts.packet_count = PING_TIME / PING_INTERVAL;
    ping_opts.interval = TAPI_JOB_OPT_DOUBLE_VAL(PING_INTERVAL);
    ping_opts.interface = iut_if->if_name;
    ping_opts.destination = dst_addr;

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));
    CHECK_RC(tapi_ping_create(factory, &ping_opts, &ping_app));

    TEST_STEP("Disable TX LPI on Tester interface.");
    te_rc = tapi_cfg_if_eee_tx_lpi_enabled_set(tst_rpcs->ta,
                                               tst_if->if_name,
                                               false);
    if (te_rc != 0)
        TEST_VERDICT("Failed to disable TX LPI on Tester");

    TEST_STEP("Disable TX LPI on IUT and check ping time.");
    ping_lpi_disabled = check_lpi_ping(ping_app, false, -1,
                                       "Disabled TX LPI");

    TEST_STEP("Enable TX LPI with big timer delay on IUT and check "
              "ping time.");
    ping_lpi_high = check_lpi_ping(ping_app, true, LPI_TIMER_HIGH,
                                   "Enabled TX LPI with big timer delay");

    TEST_STEP("Enable TX LPI with small timer delay on IUT and check "
              "ping time.");
    ping_lpi_low = check_lpi_ping(ping_app, true, LPI_TIMER_LOW,
                                  "Enabled TX LPI with small timer delay");

    cmp_r = approx_cmp(ping_lpi_high, ping_lpi_disabled);
    if (cmp_r < 0)
        WARN_VERDICT("Enabling LPI timer reduced RTT");

    cmp_r = approx_cmp(ping_lpi_high, ping_lpi_low);
    if (cmp_r > 0)
        WARN_VERDICT("Lower LPI timer delay resulted in smaller average RTT");
    else if (cmp_r == 0)
        WARN_VERDICT("Bigger LPI timer delay did not reduce RTT");

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    CLEANUP_CHECK_RC(tapi_ping_destroy(ping_app));
    tapi_job_factory_destroy(factory);
    free(dst_addr);

    TEST_END;
}
