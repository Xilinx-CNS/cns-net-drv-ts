/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-ts_info Get timestamping capabilities
 * @ingroup ethtool
 * @{
 *
 * @objective Check that ethtool provides information about packets
 *            timestamping capabilities and PHC device index.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-PTP013.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ethtool/ts_info"

#include "net_drv_test.h"

/* Expected hardware timestamping capabilities */
#define EXP_HW_TSTAMPS \
    (RPC_SOF_TIMESTAMPING_TX_HARDWARE | \
     RPC_SOF_TIMESTAMPING_RX_HARDWARE | \
     RPC_SOF_TIMESTAMPING_RAW_HARDWARE)

/* Expected software timestamping capabilities */
#define EXP_SW_TSTAMPS \
    (RPC_SOF_TIMESTAMPING_TX_SOFTWARE | \
     RPC_SOF_TIMESTAMPING_RX_SOFTWARE | \
     RPC_SOF_TIMESTAMPING_SOFTWARE)

/* Expected capabilites from rpc_hwtstamp_tx_types */
#define EXP_TX_TYPES \
    ((1 << RPC_HWTSTAMP_TX_OFF) | (1 << RPC_HWTSTAMP_TX_ON))

/* Expected capabilites from rpc_hwtstamp_rx_filters */
#define EXP_RX_FILTERS \
    ((1 << RPC_HWTSTAMP_FILTER_NONE) | (1 << RPC_HWTSTAMP_FILTER_ALL))

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    struct tarpc_ethtool_ts_info ts_info;
    struct ifreq ifreq_var;

    unsigned int not_supported;
    unsigned int extra_supported;
    te_string str_flags = TE_STRING_INIT;

    int iut_s;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    iut_s = rpc_socket(iut_rpcs, RPC_PF_INET, RPC_SOCK_DGRAM,
                       RPC_PROTO_DEF);

    memset(&ifreq_var, 0, sizeof(ifreq_var));
    strncpy(ifreq_var.ifr_name, iut_if->if_name,
            sizeof(ifreq_var.ifr_name));

    memset(&ts_info, 0, sizeof(ts_info));
    ifreq_var.ifr_data = (char *)&ts_info;
    ts_info.cmd = RPC_ETHTOOL_GET_TS_INFO;

    TEST_STEP("Call @b ioctl(@c SIOCETHTOOL / @c ETHTOOL_GET_TS_INFO) for "
              "the IUT interface.");
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_ioctl(iut_rpcs, iut_s, RPC_SIOCETHTOOL, &ifreq_var);
    if (rc < 0)
    {
        if (RPC_ERRNO(iut_rpcs) == TE_RC(TE_TA_UNIX, TE_EBADRQC))
        {
            TEST_SKIP("ioctl(SIOCETHTOOL/ETHTOOL_GET_TS_INFO) is not "
                      "supported");
        }

        TEST_VERDICT("ioctl(SIOCETHTOOL/ETHTOOL_GET_TS_INFO) failed with "
                     "error %r", RPC_ERRNO(iut_rpcs));
    }

    TEST_STEP("Check the values set in @b ethtool_ts_info structure fields "
              "after @b ioctl() call:");

    TEST_SUBSTEP("Check whether PHC device index is specified in "
                 "@b phc_index field.");
    if (ts_info.phc_index < 0)
        WARN_VERDICT("No PHC device index is specified");

    TEST_SUBSTEP("Check whether @b tx_types field tells that both "
                 "@c HWTSTAMP_TX_ON and @c HWTSTAMP_TX_OFF are supported.");
    not_supported = (~ts_info.tx_types & EXP_TX_TYPES);
    if (not_supported != 0)
    {
        CHECK_RC(hwtstamp_tx_types_flags_rpc2te_str(not_supported,
                                                    &str_flags));
        WARN_VERDICT("Some of the expected TX types flags are not set: %s",
                     str_flags.ptr);
    }

    TEST_SUBSTEP("Check whether @b tx_types field contains any "
                 "additional flags.");
    extra_supported = (ts_info.tx_types & ~EXP_TX_TYPES);
    if (extra_supported != 0)
    {
        te_string_reset(&str_flags);
        CHECK_RC(hwtstamp_tx_types_flags_rpc2te_str(extra_supported,
                                                    &str_flags));

        RING_VERDICT("Additional TX types flags are reported: %s",
                     str_flags.ptr);
    }

    TEST_SUBSTEP("Check whether @b rx_filter field tells that both "
                 "@c HWTSTAMP_FILTER_NONE and @c HWTSTAMP_FILTER_ALL "
                 "are supported.");
    not_supported = (~ts_info.rx_filters & EXP_RX_FILTERS);
    if (not_supported != 0)
    {
        te_string_reset(&str_flags);
        CHECK_RC(hwtstamp_rx_filters_flags_rpc2te_str(not_supported,
                                                      &str_flags));
        WARN_VERDICT("Some of the expected RX filters flags are not set: %s",
                     str_flags.ptr);
    }

    TEST_SUBSTEP("Check whether @b rx_filter field contains any "
                 "additional flags.");
    extra_supported = (ts_info.rx_filters & ~EXP_RX_FILTERS);
    if (extra_supported != 0)
    {
        te_string_reset(&str_flags);
        CHECK_RC(hwtstamp_rx_filters_flags_rpc2te_str(extra_supported,
                                                      &str_flags));

        RING_VERDICT("Additional RX filters flags are reported: %s",
                     str_flags.ptr);
    }

    TEST_SUBSTEP("Check whether @b so_timestamping field contains flags "
                 "for hardware timestamping capabilities.");
    not_supported = (~ts_info.so_timestamping & EXP_HW_TSTAMPS);
    if (not_supported != 0)
    {
        WARN_VERDICT("Some of the expected harware timestamps flags are "
                     "not set: %s",
                     timestamping_flags_rpc2str(not_supported));
    }

    TEST_SUBSTEP("Check whether @b so_timestamping field contains flags "
                 "for software timestamping capabilities.");
    not_supported = (~ts_info.so_timestamping & EXP_SW_TSTAMPS);
    if (not_supported != 0)
    {
        WARN_VERDICT("Some of the expected software timestamps flags are "
                     "not set: %s",
                     timestamping_flags_rpc2str(not_supported));
    }

    TEST_SUBSTEP("Check whether @b so_timestamping field contains any "
                 "additional flags.");
    /*
     * SOF_TIMESTAMPING_SYS_HARDWARE is deprecated/ignored according to
     * Linux kernel documentation, so ignore it here too.
     */
    extra_supported = (ts_info.so_timestamping & ~EXP_HW_TSTAMPS &
                       ~EXP_SW_TSTAMPS & ~RPC_SOF_TIMESTAMPING_SYS_HARDWARE);
    if (extra_supported != 0)
    {
        RING_VERDICT("NIC supports additional SO_TIMESTAMPING flags: %s",
                     timestamping_flags_rpc2str(extra_supported));
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    te_string_free(&str_flags);

    TEST_END;
}
