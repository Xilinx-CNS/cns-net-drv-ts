/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-read_sysfs Read driver sysfs files
 * @ingroup basic
 * @{
 *
 * @objective Check that the files in sysfs related to the tested
 *            driver can be read.
 *
 * @param env            Testing environment:
 *                      - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ST08.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "basic/read_sysfs"

#include "net_drv_test.h"
#include "te_file.h"
#include "te_string.h"
#include "tapi_rpc_dirent.h"
#include "tapi_rpc_unistd.h"

/**
 * Timeout in seconds for call which logs all founded files in
 * /sys/kernel/debug/[driver_module]/nic_[ifname]/
 */
#define LOG_TIMEOUT 100

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    char *iut_drv_name = NULL;

    char bdf_buf[PATH_MAX];

    te_string path = TE_STRING_INIT;
    rpc_dir_p pdir = RPC_NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    CHECK_NOT_NULL(iut_drv_name = net_drv_driver_name(iut_rpcs->ta));

    if (strcmp(iut_drv_name, NET_DRV_X3_DRIVER_NAME) == 0 ||
        strcmp(iut_drv_name, NET_DRV_IONIC_DRIVER_NAME) == 0)
    {
        char     *device_path;
        char     *bdf;
        ssize_t   ret;

        device_path = te_string_fmt("/sys/class/net/%s/device",
                                    iut_if->if_name);
        if (device_path == NULL)
            TEST_FAIL("Failed to allocate memory for device path");

        ret = rpc_readlink(iut_rpcs, device_path, bdf_buf, sizeof(bdf_buf));
        free(device_path);
        if (ret == -1)
            TEST_FAIL("Failed to read the interface's device symlink: %r",
                      te_rc_os2te(errno));
        else if (ret == sizeof(bdf_buf))
            TEST_FAIL("Device symlink contents may have been truncated");

        bdf_buf[ret + 1] = '\0';

        bdf = te_basename(bdf_buf);
        te_string_append(&path, "/sys/kernel/debug/%s/%s/", iut_drv_name, bdf);
        free(bdf);
    }
    else
    {
        te_string_append(&path, "/sys/kernel/debug/%s/nic_%s/",
                         iut_drv_name, iut_if->if_name);
    }


    TEST_STEP("Check whether driver/interface subdirectory can be found "
              "in /sys/kernel/debug/.");
    RPC_AWAIT_ERROR(iut_rpcs);
    pdir = rpc_opendir(iut_rpcs, path.ptr);
    if (pdir == RPC_NULL)
    {
        TEST_VERDICT("driver/interface subdirectory cannot be opened in "
                     "/sys/kernel/debug/");
    }
    else
    {
        rpc_closedir(iut_rpcs, pdir);
    }

    TEST_STEP("Read contents of all readable files under "
              "/sys/kernel/debug/[driver_module]/nic_[ifname]/.");
    rc = net_drv_cat_all_files(iut_rpcs, TE_SEC2MS(LOG_TIMEOUT), path.ptr);
    if (rc != 0)
        TEST_VERDICT("Failed to read files from /sys/kernel/debug/");

    te_string_reset(&path);
    te_string_append(&path, "/sys/module/%s/", iut_drv_name);

    TEST_STEP("Check whether driver subdirectory can be found "
              "in /sys/module/.");
    RPC_AWAIT_ERROR(iut_rpcs);
    pdir = rpc_opendir(iut_rpcs, path.ptr);
    if (pdir == RPC_NULL)
    {
        TEST_VERDICT("driver subdirectory cannot be opened in "
                     "/sys/module/");
    }
    else
    {
        rpc_closedir(iut_rpcs, pdir);
    }

    TEST_STEP("Read contents of all readable files under "
              "/sys/module/[driver_module]/.");
    rc = net_drv_cat_all_files(iut_rpcs, 0, path.ptr);
    if (rc != 0)
        TEST_VERDICT("Failed to read files from /sys/kernel/module/");

    TEST_SUCCESS;

cleanup:

    free(iut_drv_name);
    te_string_free(&path);

    TEST_END;
}
