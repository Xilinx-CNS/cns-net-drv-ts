/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-msglvl Change driver message level
 * @ingroup ethtool
 * @{
 *
 * @objective Check that changing driver message level affects logs
 *            printed by the driver.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ET009.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/msglvl"

#include "net_drv_test.h"
#include "tapi_cfg.h"
#include "tapi_cfg_if.h"
#include "tapi_cfg_base.h"
#include "tapi_serial_parse.h"

/*
 * This level ensures that all messages of interest appear in console
 * where this test checks for them.
 *
 * See "man klogctl" for more info. Messages are printed in console only
 * if their log level is less than console_loglevel, and the last level
 * is KERN_DEBUG=7, so we need 8 here.
 */
#define CONSOLE_LOGLEVEL 8

#define WAIT_AFTER_IF_UP \
    te_motivated_sleep(1, "wait for any driver logs")

#define WAIT_AFTER_LOGS_DISABLE \
    te_motivated_sleep(3, "wait until any logs printed before disabling " \
                       "logging are processed and msglvl change takes " \
                       "effect")

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    char *drv_name = NULL;

    tapi_parser_id id;
    int evt_count1 = 0;
    int evt_count2 = 0;
    int count_diff = 0;

    char *pci_oid = NULL;
    char *pci_bus_num = NULL;
    te_string pattern_str = TE_STRING_INIT;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    memset(&id, 0, sizeof(id));
    id.ta = "LogListener";
    id.name = "iut";

    rc = tapi_cfg_pci_oid_by_net_if(iut_rpcs->ta, iut_if->if_name,
                                    &pci_oid);
    if (rc != 0)
        TEST_SKIP("Cannot find PCI device for the tested interface");
    pci_bus_num = strstr(pci_oid, "device:") + strlen("device:");

    TEST_STEP("Configure serial parser to check for driver logs on IUT.");
    drv_name = net_drv_driver_name(iut_rpcs->ta);
    te_string_append(&pattern_str, "%s %s", drv_name, pci_bus_num);

    CHECK_RC(tapi_cfg_set_loglevel(iut_rpcs->ta, CONSOLE_LOGLEVEL));

    rc = tapi_serial_parser_event_add(&id, "driver_log", "");
    if (rc != 0)
        TEST_VERDICT("Failed to add parser event, rc=%r", rc);

    rc = tapi_serial_parser_pattern_add(&id, "driver_log",
                                        te_string_value(&pattern_str));
    if (rc < 0)
        TEST_FAIL("Failed to add a parser pattern");

    TEST_STEP("Enable all flags in @b msglvl for IUT interface.");
    rc = tapi_cfg_if_msglvl_set(iut_rpcs->ta, iut_if->if_name,
                                TAPI_NETIF_MSG_ALL);
    if (rc != 0)
        TEST_VERDICT("Failed to enable all flags in msglvl");

    TEST_STEP("Set IUT interface down and up.");
    CHECK_RC(tapi_cfg_base_if_down_up(iut_rpcs->ta, iut_if->if_name));
    WAIT_AFTER_IF_UP;

    TEST_STEP("Check that driver printed some logs.");
    CHECK_RC(tapi_serial_parser_event_get_count(&id, "driver_log",
                                                &evt_count1));
    RING("%d driver logs were detected when logging was enabled",
         evt_count1);
    if (evt_count1 <= 0)
    {
        TEST_VERDICT("No logs from the driver were detected when all "
                     "flags in msglvl are switched on");
    }

    TEST_STEP("Set @b msglvl to zero for IUT interface.");
    rc = tapi_cfg_if_msglvl_set(iut_rpcs->ta, iut_if->if_name, 0);
    if (rc != 0)
        TEST_VERDICT("Failed to set msglvl to zero");

    /*
     * Make sure that any logs printed before disabling logging
     * are processed and do not affect further checks.
     */
    WAIT_AFTER_LOGS_DISABLE;
    CHECK_RC(tapi_serial_parser_event_get_count(&id, "driver_log",
                                                &evt_count1));
    if (evt_count1 < 0)
        TEST_FAIL("Negative event count");

    TEST_STEP("Set IUT interface down and up.");
    CHECK_RC(tapi_cfg_base_if_down_up(iut_rpcs->ta, iut_if->if_name));
    WAIT_AFTER_IF_UP;

    TEST_STEP("Check that driver did not print any logs.");
    CHECK_RC(tapi_serial_parser_event_get_count(&id, "driver_log",
                                                &evt_count2));
    count_diff = evt_count2 - evt_count1;
    if (count_diff < 0)
        TEST_FAIL("Event count decreased");

    RING("%d driver logs were detected when logging was disabled",
         count_diff);

    if (count_diff > 0)
    {
        TEST_VERDICT("Some logs from driver were detected when msglvl is "
                     "set to zero");
    }

    TEST_SUCCESS;

cleanup:

    free(pci_oid);
    free(drv_name);
    TEST_END;
}
