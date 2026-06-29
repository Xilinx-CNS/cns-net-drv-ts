/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-ptp4l Use ptp4l to synchronize clocks
 * @ingroup ptp
 * @{
 *
 * @objective Check that ptp4l can be used to synchronize clocks
 *            across network.
 *
 * @param env              Testing environment:
 *                         - @ref env-peer2peer
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "ptp/ptp4l"

#include <math.h>

#include "net_drv_test.h"
#include "tapi_job.h"
#include "tapi_job_factory_rpc.h"

/* How long to run ptp4l, in seconds */
#define RUN_TIME 100

/* How long to wait for ptp4l termination, in ms */
#define JOB_WAIT_TIMEOUT 100

/* Minimum initial value of tv_sec field */
#define MIN_SEC 0
/* Maximum initial value of tv_sec field */
#define MAX_SEC 1000000000
/*
 * Minimum initial difference in timestamps between IUT and Tester, in
 * seconds
 */
#define MIN_DIFF 2

/*
 * Maximum difference in seconds between IUT and Tester clocks
 * after running ptp4l.
 */
#define MAX_DIFF_AFTER_RUN 0.2

/*
 * Try to find ptp4l application.
 * If it is missing, test should be skipped.
 */
static void
find_ptp4l(rcf_rpc_server *rpcs, const char *where)
{
    rpc_wait_status st;

    RPC_AWAIT_ERROR(rpcs);
    st = rpc_system(rpcs, "which ptp4l");
    if (st.flag != RPC_WAIT_STATUS_EXITED || st.value != 0)
        TEST_SKIP("Cannot find ptp4l on %s", where);
}

/* Check messages printed to stderr by ptp4l. */
static void
check_errs(tapi_job_channel_t *err_chan)
{
    tapi_job_buffer_t *bufs = NULL;
    unsigned int count = 0;
    te_errno rc;
    unsigned int i;

    te_bool tx_timeout = FALSE;
    te_bool sync_without_ts = FALSE;
    te_bool unknown = FALSE;

    const char *tx_timeout_str =
          "timed out while polling for tx timestamp";
    const char *sync_without_ts_str =
          "received SYNC without timestamp";
    const char *msg = NULL;

    rc = tapi_job_receive_many(
                    TAPI_JOB_CHANNEL_SET(err_chan),
                    0, &bufs, &count);
    if (rc != 0)
    {
        WARN_VERDICT("tapi_job_receive_many() returned %r "
                     "when retrieving error messages", rc);
    }

    for (i = 0; i < count; i++)
    {
        if (bufs[i].eos)
            break;

        msg = te_string_value(&bufs[i].data);
        if (strstr(msg, tx_timeout_str) != NULL)
            tx_timeout = TRUE;
        else if (strstr(msg, sync_without_ts_str) != NULL)
            sync_without_ts = TRUE;
        else
            unknown = TRUE;
    }

    tapi_job_buffers_free(bufs, count);

    if (tx_timeout)
        WARN_VERDICT("Error on IUT: %s", tx_timeout_str);
    if (sync_without_ts)
        WARN_VERDICT("Error on IUT: %s", sync_without_ts_str);
    if (unknown)
        WARN_VERDICT("Unexpected error was logged on IUT");
}

/* Wait for ptp4l termination, check its exit code. */
static void
wait_ptp4l(tapi_job_t *job, const char *where)
{
    tapi_job_status_t st;

    CHECK_RC(tapi_job_wait(job, JOB_WAIT_TIMEOUT, &st));
    if (st.type != TAPI_JOB_STATUS_EXITED)
    {
        WARN_VERDICT("ptp4l terminated in unexpected way on %s", where);
    }
    else if (st.value != 0)
    {
        WARN_VERDICT("ptp4l terminated with nonzero exit code on %s",
                     where);
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;

    tapi_job_factory_t *iut_factory = NULL;
    tapi_job_factory_t *tst_factory = NULL;
    tapi_job_t *master_job = NULL;
    tapi_job_t *slave_job = NULL;
    tapi_job_channel_t *master_channels[2] = { NULL, NULL };
    tapi_job_channel_t *slave_channels[2] = { NULL, NULL };
    tapi_job_channel_t *slave_err_filter = NULL;

    int fd_iut = -1;
    int fd_tst = -1;
    tarpc_timespec ts_iut;
    tarpc_timespec ts_tst;
    double diff;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd_iut, "IUT");

    TEST_STEP("Find out PTP device associated with the Tester interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(tst_rpcs, tst_if->if_name, &fd_tst, "Tester");

    TEST_STEP("Check that @b ptp4l is available on IUT and Tester.");
    find_ptp4l(iut_rpcs, "IUT");
    find_ptp4l(tst_rpcs, "Tester");

    TEST_STEP("Disable @b NTP daemon on IUT and Tester.");
    net_drv_ptp_ntpd_disable(iut_rpcs);
    net_drv_ptp_ntpd_disable(tst_rpcs);

    TEST_STEP("Use @b clock_settime() on IUT and Tester to set different "
              "initial values for IUT and Tester clocks.");

    while (TRUE)
    {
        ts_iut.tv_sec = rand_range(MIN_SEC, MAX_SEC);
        ts_iut.tv_nsec = rand_range(0, 999999999);

        ts_tst.tv_sec = rand_range(MIN_SEC, MAX_SEC);
        ts_tst.tv_nsec = rand_range(0, 999999999);

        diff = net_drv_timespec_diff(&ts_iut, &ts_tst);
        if (fabs(diff) >= MIN_DIFF)
            break;
    }

    rpc_clock_settime(iut_rpcs, TARPC_CLOCK_ID_FD, fd_iut, &ts_iut);
    rpc_clock_settime(tst_rpcs, TARPC_CLOCK_ID_FD, fd_tst, &ts_tst);

    RING("Initial difference between device clocks: %f", diff);

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs,
                                         &iut_factory));
    CHECK_RC(tapi_job_factory_rpc_create(tst_rpcs,
                                         &tst_factory));

    CHECK_RC(tapi_job_create(
               tst_factory, NULL, "ptp4l",
               (const char *[]){"ptp4l", "-m", "-i", tst_if->if_name,
               NULL}, NULL, &master_job));

    CHECK_RC(tapi_job_create(
               iut_factory, NULL, "ptp4l",
               (const char *[]){"ptp4l", "-m", "-s", "-i", iut_if->if_name,
               NULL}, NULL, &slave_job));

    CHECK_RC(tapi_job_alloc_output_channels(master_job, 2,
                                            master_channels));
    CHECK_RC(tapi_job_alloc_output_channels(slave_job, 2,
                                            slave_channels));

    CHECK_RC(tapi_job_attach_filter(
                TAPI_JOB_CHANNEL_SET(master_channels[0]),
                "Master_stdout", FALSE, TE_LL_RING, NULL));

    CHECK_RC(tapi_job_attach_filter(
                TAPI_JOB_CHANNEL_SET(master_channels[1]),
                "Master_stderr", FALSE, TE_LL_WARN, NULL));

    CHECK_RC(tapi_job_attach_filter(
                TAPI_JOB_CHANNEL_SET(slave_channels[0]),
                "Slave_stdout", FALSE, TE_LL_RING, NULL));

    CHECK_RC(tapi_job_attach_filter(
                TAPI_JOB_CHANNEL_SET(slave_channels[1]),
                "Slave_stderr", TRUE, TE_LL_WARN, &slave_err_filter));

    TEST_STEP("Run @b ptp4l as master on Tester.");
    CHECK_RC(tapi_job_start(master_job));

    TEST_STEP("Run @b ptp4l as slave on IUT.");
    CHECK_RC(tapi_job_start(slave_job));

    TEST_STEP("Wait for a while and terminate @b ptp4l on IUT and Tester.");
    SLEEP(RUN_TIME);

    CHECK_RC(tapi_job_kill(master_job, RPC_SIGINT));
    CHECK_RC(tapi_job_kill(slave_job, RPC_SIGINT));

    wait_ptp4l(master_job, "Tester");
    wait_ptp4l(slave_job, "IUT");

    TEST_STEP("Check whether anything was printed to stderr on IUT.");
    check_errs(slave_err_filter);

    TEST_STEP("Obtain current time on the IUT clock and on the Tester "
              "clock, check that obtained values are (approximately) "
              "equal.");

    rpc_clock_gettime(iut_rpcs, TARPC_CLOCK_ID_FD, fd_iut, &ts_iut);
    rpc_clock_gettime(tst_rpcs, TARPC_CLOCK_ID_FD, fd_tst, &ts_tst);
    diff = net_drv_timespec_diff(&ts_iut, &ts_tst);

    RING("Final difference between device clocks: %f",
         diff);

    if (fabs(diff) > MAX_DIFF_AFTER_RUN)
        TEST_VERDICT("ptp4l did not synchronize clocks well");

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, fd_iut);
    CLEANUP_RPC_CLOSE(tst_rpcs, fd_tst);

    CLEANUP_CHECK_RC(tapi_job_destroy(master_job, 0));
    CLEANUP_CHECK_RC(tapi_job_destroy(slave_job, 0));
    tapi_job_factory_destroy(iut_factory);
    tapi_job_factory_destroy(tst_factory);

    TEST_END;
}
