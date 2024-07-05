# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

project('net-drv-ts', 'c',
    version : '1.0.0',
    default_options: 'werror=true'
)

te_path = get_option('te_path')
te_libdir = get_option('te_libdir')
add_project_arguments(get_option('te_cflags').split(), language: 'c')
add_project_link_arguments(get_option('te_ldflags').split(), language: 'c')

cc = meson.get_compiler('c')

te_tests_info_sh = find_program(join_paths(te_path, 'te_tests_info.sh'))

test_deps = [ dependency('threads'), cc.find_library('m') ]

te_libs = [
    'tapi_bpf',
    'tapi_env',
    'tapi_rpc',
    'rpc_types',
    'rcfrpc',
    'rpcxdr',
    'tapi',
    'tapi_tad',
    'confapi',
    'conf_oid',
    'rcfapi',
    'logger_ten',
    'ipc',
    'tools',
    'logger_core',
    'ndn',
    'asn',
    'tapi_job',
    'tapi_tool',
    'tapi_performance',
]

foreach lib : te_libs
    test_deps += cc.find_library(lib, dirs: te_libdir)
endforeach

lib_dir = include_directories('lib')

# The line below would produce empty dependencies on systems
# having no tirpc - it is not a problem.
dep_tirpc = dependency('libtirpc', required: false)

subdir('lib')

test_deps += declare_dependency(include_directories: lib_dir,
                                link_with: [ ts_lib ])

test_deps += [ dep_tirpc ]

tests = [ 'prologue' ]

package_dir = 'net-drv-ts'
package_tests_c = []

foreach test : tests
    test_exe = test
    test_c = test + '.c'
    package_tests_c += [ test_c ]
    executable(test_exe, test_c, install: true, install_dir: package_dir,
               dependencies: test_deps)
endforeach

install_data([ 'package.xml', 'package.dox' ],
             install_dir: package_dir)

packages = [
    'basic',
    'devlink',
    'ethtool',
    'offload',
    'perf',
    'ptp',
    'rss',
    'rx_path',
    'stress',
]

mydir = package_dir
foreach package : packages
    package_dir = join_paths(mydir, package)
    package_tests_c = []
    subdir(package)
endforeach
