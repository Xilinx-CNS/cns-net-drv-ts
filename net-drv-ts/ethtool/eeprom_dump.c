/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2024 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-eeprom_dump Obtain EEPROM dump
 * @ingroup ethtool
 * @{
 *
 * @objective Check that EEPROM dump can be obtained.
 *
 * @param env            Testing environment:
 *                       - @ref env-iut_only
 *
 * @par Scenario:
 *
 * @author Andrew Rybchenko <andrew.rybchenko@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/eeprom_dump"

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

    TEST_STEP("Obtain EEPROM dump with Ethtool on @p iut_if.");
    opts.cmd = TAPI_ETHTOOL_CMD_EEPROM_DUMP;
    opts.timeout_ms = TE_SEC2MS(10);
    opts.if_name = iut_if->if_name;
    opts.args.eeprom_dump.length = TAPI_JOB_OPT_UINT_VAL(1024);

    rc = tapi_ethtool(factory, &opts, &report);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_TAPI, TE_ESHCMD))
            ERROR_VERDICT("Ethtool command did not terminate successfully");
        else
            TEST_VERDICT("tapi_ethtool() returned unexpected error");
    }

    TEST_STEP("Check that nothing was printed to stderr.");
    if (report.err_out)
    {
        if (report.err_code == TE_EOPNOTSUPP)
        {
            TEST_VERDICT("Getting EEPROM dump is not supported");
        }
        else
        {
            ERROR_VERDICT("ethtool printed something to stderr");
        }
    }

    TEST_STEP("Check that something was printed to stdout.");
    if (!report.out)
        TEST_VERDICT("Nothing was printed to stdout");

    TEST_SUCCESS;

cleanup:

    tapi_job_factory_destroy(factory);
    tapi_ethtool_destroy_report(&report);

    TEST_END;
}
