/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Common test API
 *
 * Implementation of TAPI for checking ethtool.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#include "tapi_test.h"
#include "tapi_rpc_stdio.h"
#include "tapi_rpc_unistd.h"
#include "tapi_tags.h"
#include "tarpc.h"
#include "net_drv_ethtool.h"

#if HAVE_NET_IF_H
#include <net/if.h>
#endif

#define NET_DRV_ETHTOOL_MISS_OPT_TAG_PREFIX "no-ethtool-opt-"

/* CLI options used in TS tests to run ethtool. */
static const char * const net_drv_ethtool_test_opts[] = {
    "--statistics",
    "--show-pause",
    "--show-ring",
    "--register-dump",
    "--eeprom-dump",
    "--dump-module-eeprom",
    "--show-eee",
    "--show-fec",
    "--show-module",
};

/* See net_drv_ethtool.h */
int
net_drv_ethtool_reset(rcf_rpc_server *rpcs, int s, const char *if_name,
                      unsigned int flags, unsigned int *ret_flags)
{
    struct ifreq ifreq_var;
    struct tarpc_ethtool_value ethtool_val;
    int rc;

    memset(&ifreq_var, 0, sizeof(ifreq_var));
    CHECK_RC(te_snprintf(ifreq_var.ifr_name, sizeof(ifreq_var.ifr_name),
                         "%s", if_name));

    memset(&ethtool_val, 0, sizeof(ethtool_val));
    ifreq_var.ifr_data = (char *)&ethtool_val;
    ethtool_val.cmd = RPC_ETHTOOL_RESET;
    ethtool_val.data = flags;

    rc = rpc_ioctl(rpcs, s, RPC_SIOCETHTOOL, &ifreq_var);
    if (ret_flags != NULL)
        *ret_flags = ethtool_val.data;

    return rc;
}

static te_errno
net_drv_ethtool_get_help(rcf_rpc_server *rpcs, te_string *help)
{
    char *output[2] = { NULL, NULL };
    rpc_wait_status status;
    te_errno rc = 0;

    rpcs->silent_pass = rpcs->silent_pass_default = true;

    status = rpc_shell_get_all3(rpcs, output, "ethtool --help");
    if (status.flag != RPC_WAIT_STATUS_EXITED || status.value != 0)
    {
        ERROR("ethtool --help terminated unexpectedly on TA %s (flag=%d, value=%d)",
              rpcs->ta, status.flag, status.value);
        rc = TE_EFAIL;
        goto cleanup;
    }

    if (output[0] != NULL)
        te_string_append(help, "%s", output[0]);

    if (output[1] != NULL)
        te_string_append(help, "%s", output[1]);

cleanup:
    free(output[0]);
    free(output[1]);

    rpcs->silent_pass = rpcs->silent_pass_default = false;

    return rc;
}

/* See net_drv_ethtool.h */
te_errno
net_drv_add_missing_ethtool_opt_tags(rcf_rpc_server *rpcs)
{
    te_string help = TE_STRING_INIT;
    te_string tag = TE_STRING_INIT;
    const char *help_str;
    const char *opt_name;
    unsigned int i;
    te_errno rc;

    rc = net_drv_ethtool_get_help(rpcs, &help);
    if (rc != 0)
        goto cleanup;

    help_str = te_string_value(&help);

    for (i = 0; i < TE_ARRAY_LEN(net_drv_ethtool_test_opts); i++)
    {
        opt_name = net_drv_ethtool_test_opts[i];

        if (strstr(help_str, opt_name) != NULL)
            continue;

        te_string_reset(&tag);
        te_string_append(&tag, "%s%s", NET_DRV_ETHTOOL_MISS_OPT_TAG_PREFIX,
                         opt_name + 2);

        INFO("ethtool CLI option '%s' is not available, add TRC tag '%s'",
             opt_name, te_string_value(&tag));

        rc = tapi_tags_add_tag(te_string_value(&tag), NULL);
        if (rc != 0)
            goto cleanup;
    }

cleanup:
    te_string_free(&tag);
    te_string_free(&help);
    return rc;
}
