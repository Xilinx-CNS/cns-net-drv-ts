# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

sources = [
    'net_drv_data_flow.c',
    'net_drv_ethtool.c',
    'net_drv_phy.c',
    'net_drv_ptp.c',
    'net_drv_rpc.c',
    'net_drv_ts.c',
]

ts_lib = static_library('ts_net_drv', sources,
                        include_directories: lib_dir,
                        dependencies: dep_tirpc)
