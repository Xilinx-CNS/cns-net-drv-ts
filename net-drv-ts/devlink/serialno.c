/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2025 Advanced Micro Devices, Inc. */
/*
 * Net Driver Test Suite
 * Devlink tests
 */

/**
 * @defgroup devlink-serialno Check device serial number
 * @ingroup devlink
 * @{
 *
 * @objective Check that device serial number is non-empty string.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "devlink/serialno"

#include "net_drv_test.h"
#include "tapi_cfg_pci.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    char *pci_oid = NULL;
    char *val_str = NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    rc = tapi_cfg_pci_oid_by_net_if(iut_rpcs->ta, iut_if->if_name,
                                    &pci_oid);
    if (rc != 0)
        TEST_SKIP("Cannot find PCI device for the tested interface");

    TEST_STEP("Check that serial number can be retrieved for the "
              "IUT interface.");
    rc = cfg_get_string(&val_str, "%s/serialno:", pci_oid);
    if (rc != 0)
        TEST_VERDICT("Cannot get serial number");

    TEST_STEP("Check that serial number is non-empty string.");
    if (te_str_is_null_or_empty(val_str))
        TEST_VERDICT("Serial number is absent");

    RING("Serial number: %s", val_str);

    TEST_SUCCESS;

cleanup:

    free(pci_oid);
    free(val_str);

    TEST_END;
}
