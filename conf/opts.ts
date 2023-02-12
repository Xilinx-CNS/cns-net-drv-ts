# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.
--conf-cs=cs/net-drv-ts.yml
--script=scripts/ta-def
--script=scripts/defaults
--script=scripts/disable_unused_agts
--script=scripts/export_driver_sources
--script=scripts/check_bpf
--tester-script=scripts/os-trc-tags
--script=scripts/def-core-pattern
--script=scripts/core_watchers
