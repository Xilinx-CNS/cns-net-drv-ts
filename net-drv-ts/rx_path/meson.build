# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

tests = [
    'rx_coalesce_frames',
    'rx_coalesce_usecs',
    'rx_fcs',
]

rx_test_deps = test_deps
rx_test_deps += [ cc.find_library('z') ]
rx_test_deps += [ cc.find_library('m') ]

foreach test : tests
    test_exe = test
    test_c = test + '.c'
    package_tests_c += [ test_c ]
    executable(test_exe, test_c, install: true, install_dir: package_dir,
               dependencies: rx_test_deps)
endforeach

tests_info_xml = custom_target(package_dir.underscorify() + 'tests-info-xml',
                               install: true, install_dir: package_dir,
                               input: package_tests_c,
                               output: 'tests-info.xml', capture: true,
                               command: [ te_tests_info_sh,
                               meson.current_source_dir() ])

install_data([ 'package.xml', 'package.dox' ],
             install_dir: package_dir)
