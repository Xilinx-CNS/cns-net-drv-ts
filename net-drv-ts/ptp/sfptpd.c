/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-sfptpd Use sfptpd to synchronize clocks
 * @ingroup ptp
 * @{
 *
 * @objective Check that sfptpd can be used to synchronize clocks
 *            across network.
 *
 * @param env              Testing environment:
 *                         - @ref env-peer2peer
 * @param master_cfg       Configuration file for sfptpd as a PTP master
 * @param slave_cfg        Configuration file for sfptpd as a PTP slave
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-PTP012.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ptp/sfptpd"

#include "net_drv_test.h"
#include "tapi_ntpd.h"
#include "tapi_sfptpd.h"
#include "tapi_ethtool.h"
#include "tapi_job_factory_rpc.h"

/* Maximum time to wait for clocks sychronization, in seconds */
#define MAX_SYNC_WAIT 1200

/*
 * How long to wait before checking (again) clock synchronization
 * state, in seconds.
 */
#define CHECK_WAIT 5

/*
 * Set configuration parameters necessary to run sfptpd,
 * copy binary and configuration file to TA host.
 */
static void
config_sfptpd(const char *ta, const char *if_name,
              const char *cfg)
{
    char *agt_dir = NULL;
    char *sfptpd_path = NULL;
    te_string rem_path = TE_STRING_INIT_STATIC(1024);
    te_errno rc;

    rc = cfg_get_instance_string_fmt(&sfptpd_path,
                                     "/local:/path:sfptpd");
    if (rc != 0 || sfptpd_path == NULL || *sfptpd_path == '\0')
    {
        TEST_SKIP("Path to sfptpd cannot be obtained from "
                  "/local:/path:sfptpd");
    }

    CHECK_RC(cfg_get_instance_string_fmt(&agt_dir,
                                         "/agent:%s/dir:", ta));

    te_string_append(&rem_path, "%s/sfptpd", agt_dir);

    RING("Copy %s to %s:%s", sfptpd_path, ta, rem_path.ptr);
    CHECK_RC(rcf_ta_put_file(ta, 0, sfptpd_path, rem_path.ptr));
    CHECK_RC(cfg_set_instance_fmt(CVT_STRING, rem_path.ptr,
                                  "/agent:%s/sfptpd:/path:", ta));

    CHECK_RC(cfg_set_instance_fmt(CVT_STRING, if_name,
                                  "/agent:%s/sfptpd:/ifname:", ta));

    te_string_reset(&rem_path);
    te_string_append(&rem_path, "%s/sfptpd.cfg", agt_dir);

    RING("Copy %s to %s:%s", cfg, ta, rem_path.ptr);
    CHECK_RC(rcf_ta_put_file(ta, 0, cfg, rem_path.ptr));

    CHECK_RC(cfg_set_instance_fmt(CVT_STRING, rem_path.ptr,
                                  "/agent:%s/sfptpd:/config:", ta));

    free(agt_dir);
    free(sfptpd_path);
}

/*
 * Get synchronization status from a given status file.
 * "fd" parameter here is used to allow closing
 * opened FD in cleanup if this function fails.
 */
static void
get_sync_status(rcf_rpc_server *rpcs, const char *path,
                int *fd, te_bool *in_sync)
{
    te_string str = TE_STRING_INIT;
    char sync_pref[] = "in-sync:";
    char *p;
    size_t i;

    *in_sync = FALSE;

    /*
     * I reopen the file again and again instead of doing it
     * once because it appears that sfptpd can replace the current
     * file with a new one having a different inode, so that
     * previously opened FD will not observe new changes.
     */
    rpcs->silent = TRUE;
    RPC_AWAIT_ERROR(rpcs);
    *fd = rpc_open(rpcs, path, RPC_O_RDONLY, 0);
    if (*fd < 0)
    {
        /* If the file is not created yet, it is OK. */
        if (RPC_ERRNO(rpcs) == RPC_ENOENT)
            return;

        TEST_VERDICT("Failed to open %s, errno=%r", path, RPC_ERRNO(rpcs));
    }

    rpcs->silent_pass = TRUE;
    rpc_read_fd2te_string(rpcs, *fd, 0, 0, &str);

    for (i = 0; i < str.len; i++)
    {
        if (isspace(str.ptr[i]) &&
            strncmp(str.ptr + i + 1, sync_pref, sizeof(sync_pref) - 1) == 0)
        {
            /*
             * The first symbol of interest is at str.ptr + i + 1,
             * however sizeof(sync_pref) = strlen(sync_pref) + 1,
             * so "+ 1" is not present here.
             */
            p = str.ptr + i + sizeof(sync_pref);
            *in_sync = (atoi(p) == 0 ? FALSE : TRUE);
            break;
        }
    }

    te_string_free(&str);
    rpcs->silent_pass = TRUE;
    RPC_CLOSE(rpcs, *fd);
}

/* Get sum of all ethtool statistics with prefix "ptp_". */
static int64_t
get_ptp_stats_sum(tapi_job_factory_t *factory, tapi_ethtool_opt *opts,
                  tapi_ethtool_report *report, const char *stage)
{
    te_kvpair *kv = NULL;
    te_kvpair *kv_aux = NULL;
    long unsigned int sum = 0;
    long unsigned int val;
    int rc;

    rc = tapi_ethtool(factory, opts, report);
    if (rc != 0)
    {
        ERROR_VERDICT("%s: failed to process ethtool command, "
                      "rc=%r", stage, rc);
        return -1;
    }

    if (report->err_out)
        ERROR_VERDICT("%s: ethtool printed something to stderr", stage);

    TAILQ_FOREACH_SAFE(kv, &report->data.stats, links, kv_aux)
    {
        if (strcmp_start("ptp_", kv->key) == 0)
        {
            CHECK_RC(te_strtoul(kv->value, 10, &val));
            sum += val;
        }
        else
        {
            TAILQ_REMOVE(&report->data.stats, kv, links);
        }
    }

    RING("%s, sum of PTP statistics: %lu", stage, sum);
    return sum;
}

/*
 * Log statistics which changed between two ethtool reports.
 * Note: this function destroys some data in reports.
 */
static void
print_stats_diff(tapi_ethtool_report *before, tapi_ethtool_report *after)
{
#define ROW_FMT "%30s%20s%20s\n"
    te_kvpair *kv_before = NULL;
    te_kvpair *kv_aux = NULL;
    te_kvpair *kv_after = NULL;
    te_bool found;
    te_string str = TE_STRING_INIT;

    te_string_append(&str, ROW_FMT, "Name", "Before", "After");

    /*
     * Print statistics which changed or disappeared in the second
     * report.
     */
    TAILQ_FOREACH_SAFE(kv_before, &before->data.stats, links, kv_aux)
    {
        found = FALSE;
        TAILQ_FOREACH(kv_after, &after->data.stats, links)
        {
            if (strcmp(kv_after->key, kv_before->key) == 0)
            {
                found = TRUE;
                break;
            }
        }

        if (!found || strcmp(kv_before->value, kv_after->value) != 0)
        {
            te_string_append(&str, ROW_FMT,
                             kv_before->key, kv_before->value,
                             (found ? kv_after->value : "NONE"));
        }

        TAILQ_REMOVE(&before->data.stats, kv_before, links);
        if (found)
            TAILQ_REMOVE(&after->data.stats, kv_after, links);
    }

    /* Print statistics which appeared only in the second report. */
    TAILQ_FOREACH(kv_after, &after->data.stats, links)
    {
        te_string_append(&str, ROW_FMT, kv_after->key,
                         "NONE", kv_after->value);
    }

    RING("Changed PTP statistics:\n%s",
         te_string_value(&str));
    te_string_free(&str);
#undef ROW_FMT
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;

    int ptp_state_fd = -1;
    int system_state_fd = -1;
    te_bool ptp_in_sync = FALSE;
    te_bool system_in_sync = FALSE;

    char *master_cfg = NULL;
    char *slave_cfg = NULL;

    int i;
    int fd = -1;

    tapi_job_factory_t *factory = NULL;
    tapi_ethtool_opt opts = tapi_ethtool_default_opt;
    tapi_ethtool_report report_before = tapi_ethtool_default_report;
    tapi_ethtool_report report_after = tapi_ethtool_default_report;
    int64_t stats_before = 0;
    int64_t stats_after = 0;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_FILENAME_PARAM(master_cfg);
    TEST_GET_FILENAME_PARAM(slave_cfg);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "check that it can be opened to be sure that PTP is "
              "supported.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd, "");
    RPC_CLOSE(iut_rpcs, fd);

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));
    opts.cmd = TAPI_ETHTOOL_CMD_STATS;
    opts.if_name = iut_if->if_name;

    TEST_STEP("Compute sum of statistics from 'ethtool -S' output "
              "having 'ptp_' prefix.");
    stats_before = get_ptp_stats_sum(factory, &opts, &report_before,
                                     "Before running sfptpd");

    config_sfptpd(iut_rpcs->ta, iut_if->if_name, slave_cfg);
    config_sfptpd(tst_rpcs->ta, tst_if->if_name, master_cfg);

    TEST_STEP("Disable @b NTP daemon on IUT and Tester.");
    tapi_ntpd_disable(iut_rpcs);
    tapi_ntpd_disable(tst_rpcs);

    TEST_STEP("Start @b sfptpd on IUT and Tester. On IUT it is configured "
              "to be a slave synchronizing time with master on Tester.");
    tapi_sfptpd_enable(iut_rpcs->ta);
    tapi_sfptpd_enable(tst_rpcs->ta);

    TEST_STEP("Wait until both adapter clock and system clock are reported "
              "to be in-sync by @b sfptpd on IUT.");
    for (i = 0; i < MAX_SYNC_WAIT / CHECK_WAIT + 1; i++)
    {
        /* Use simple sleep() since additional log is not desirable here */
        sleep(CHECK_WAIT);

        get_sync_status(iut_rpcs, "/var/lib/sfptpd/state-ptp1",
                        &ptp_state_fd, &ptp_in_sync);
        get_sync_status(iut_rpcs, "/var/lib/sfptpd/state-system",
                        &system_state_fd, &system_in_sync);

        RING("State reported in /var/lib/sfptpd/\n"
             "ptp1.in-sync: %s\n"
             "system.in-sync: %s\n",
             (ptp_in_sync ? "TRUE" : "FALSE"),
             (system_in_sync ? "TRUE" : "FALSE"));

        if (system_in_sync && ptp_in_sync)
            break;
    }

    if (!system_in_sync)
    {
        ERROR_VERDICT("Failed to wait until system clock becomes "
                      "synchronized");
    }

    if (!ptp_in_sync)
    {
        ERROR_VERDICT("Failed to wait until adapter clock becomes "
                      "synchronized");
    }

    TEST_STEP("Again compute sum of statistics from 'ethtool -S' output "
              "having 'ptp_' prefix. Check that after running sfptpd "
              "this sum increased.");

    stats_after = get_ptp_stats_sum(factory, &opts, &report_after,
                                    "After running sfptpd");

    if (stats_after >= 0 && stats_before >= 0)
    {
        print_stats_diff(&report_before, &report_after);

        if (stats_after <= stats_before)
        {
            ERROR_VERDICT("Sum of PTP statistics after running sfptpd %s",
                          (stats_after < stats_before ?
                                "decreased" : "did not increase"));
        }
    }

    if (!system_in_sync || !ptp_in_sync)
        TEST_STOP;

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, ptp_state_fd);
    CLEANUP_RPC_CLOSE(iut_rpcs, system_state_fd);
    free(master_cfg);
    free(slave_cfg);

    tapi_job_factory_destroy(factory);
    tapi_ethtool_destroy_report(&report_before);
    tapi_ethtool_destroy_report(&report_after);

    TEST_END;
}
