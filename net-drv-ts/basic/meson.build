# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

tests = [
    'driver_info',
    'ethtool_reset_nic',
    'mac_change_rx',
    'mac_change_tx',
    'mtu_tcp',
    'mtu_udp',
    'multicast',
    'ping',
    'read_sysfs',
    'rx_mode',
    'send_receive',
    'set_if_down',
    'set_rx_headroom',
]

foreach test : tests
    test_exe = test
    test_c = test + '.c'
    package_tests_c += [ test_c ]
    executable(test_exe, test_c, install: true, install_dir: package_dir,
               dependencies: test_deps)
endforeach

tests_info_xml = custom_target(package_dir.underscorify() + 'tests-info-xml',
                               install: true, install_dir: package_dir,
                               input: package_tests_c,
                               output: 'tests-info.xml', capture: true,
                               command: [ te_tests_info_sh,
                               meson.current_source_dir() ])

install_data([ 'package.xml', 'package.dox' ],
             install_dir: package_dir)
