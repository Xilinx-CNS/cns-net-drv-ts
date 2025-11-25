#! /bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.
#
# Helper script to run Test Environment for the Test Suite

set -e
source "$(dirname "$(which "$0")")"/guess.sh

source "${TE_BASE}/scripts/lib"
source "${TE_BASE}/scripts/lib.grab_cfg"
source "${TE_BASE}/scripts/lib.meta"

if [[ -e "${TE_TS_RIGSDIR}/scripts/lib/grab_cfg_handlers" ]] ; then
    source "${TE_TS_RIGSDIR}/scripts/lib/grab_cfg_handlers"
fi

TE_RUN_META=yes
CFG=

run_fail() {
    echo "$*" >&2
    exit 1
}

cleanup() {
    call_if_defined grab_cfg_release
}
trap "cleanup" EXIT

usage() {
    cat <<EOF
USAGE: run.sh [run.sh options] [dispatcher.sh options]
Options:
  --cfg=<CFG>               Configuration to be used.
  --cfg=<CFG>:vm-pt[:VM_NAME]
                            Run VM on IUT with NIC under test PCI functions
                            passed to it.
                            VM_NAME is a virtual machine name mapped to disk
                            image and build host via environment variables
                            (see TE_TS_RIGSDIR/env/vms).
  --cfg=virtio_virtio[:HV][:VM_NAME]
                            Run on a pair of dynamically started VMs connected
                            via Linux bridge. VMs are started on HV host
                            (localhost by default).
                            VM_NAME is a virtual machine name mapped to disk
                            image and build host via environment variables
                            (x86_64_debian11 by default, see ts-conf/README.md).
EOF

    call_if_defined grab_cfg_print_help

    cat <<EOF
  --reuse-pco               Do not restart RPC servers in each test
                            (it makes testing significantly faster)
  --phy-autoneg=(on|off)
                            PHY link autonegatiation state to be used by each
                            interface on IUT and TST (the same as
                            --iut-phy-autoneg and --tst-phy-autoneg at the same
                            time)
  --phy-duplex=(full|half)
                            PHY link duplex mode to be used by each
                            interface on IUT and TST (the same as
                            --iut-phy-duplex and --tst-phy-duplex at the same
                            time)
  --phy-speed=(10|100|1000|10000)
                            PHY link speed to be used by each interface on IUT
                            and TST (the same as --iut-phy-speed and
                            --tst-phy-speed at the same time)
  --iut-phy-autoneg=(on|off)
                            PHY link autonegatiation state to be used by each
                            interface on IUT
  --iut-phy-duplex=(full|half)
                            PHY link duplex mode to be used by each
                            interface on IUT
  --iut-phy-speed=(10|100|1000|10000)
                            PHY link speed to be used by each interface on IUT
  --tst-phy-autoneg=(on|off)
                            PHY link autonegatiation state to be used by each
                            interface on TST
  --tst-phy-duplex=(full|half)
                            PHY link duplex mode to be used by each
                            interface on TST
  --tst-phy-speed=(10|100|1000|10000)
                            PHY link speed to be used by each interface on TST
  --net-driver-ndebug       Build net drivers with NDEBUG=1 option
  --no-meta                 Do not generate testing metadata
  --publish                 Publish testing logs to Bublik

EOF
    "${TE_BASE}"/dispatcher.sh --help
    exit 1
}

function process_virtio() {
    local hpv_var=$1 ; shift
    local hypervisor=$1
    local vm_name=$2

    test -z "${hypervisor}" || eval export "${hpv_var}"="${hypervisor}"
    test -z "${vm_name}" || export TE_VM_NAME="${vm_name}"

    if test -z "${!hpv_var}" ; then
        eval export "${hpv_var}"_LOCALHOST=true
        eval export "${hpv_var}"_BUILD=local
    fi
}

#######################################
# Process configuration modifiers
# Globals:
#   TE_VM_NAME
#   MOD_OPTS
# Arguments:
#   Configuration name (mandatory)
#   Modifier - 'vm-pt' for now (mandatory)
#   VM name to use, see ts-rigs/env/ta-build (optional)
#######################################
function process_cfg_modifiers() {
    local cfg="$1" ; shift
    local modifier="$1" ; shift
    local vm_name="$1"

    if [[ "$modifier" != "vm-pt" ]] ; then
        run_fail "ERROR: unsupported '$modifier' modifier"
    fi

    [[ -z "$vm_name" ]] || export TE_VM_NAME="$vm_name"

    # site-specific TA build configuration for VMs
    MOD_OPTS+=(--script=env/ta-build)
    # site-specific tuning of VMs configuration
    MOD_OPTS+=(--script=env/vms)
    # generic ts-conf options to enable VM pass-through
    MOD_OPTS+=(--opts=opts/vm_pt)
}

#######################################
# Process the configuration that was passed to the script using the --cfg option
# Globals:
#   RUN_OPTS
# Arguments:
#   Configuration name (mandatory)
#   Variants of the second argument:
#     - empty
#     - hypervisor name, if the previous argument was virtio_virtio
#     - vm-pt - for testing VM on IUT with PCI pass-through
#######################################
function process_cfg() {
    local cfg="$1" ; shift
    local modifier="$1"
    local run_conf
    local -a MOD_OPTS

    case "${cfg}" in
        virtio_virtio)
            process_virtio TE_HYPERVISOR "$@" ;;
        *)
            call_if_defined grab_cfg_process "${cfg}" || exit 1
            [[ -z "$modifier" ]] || process_cfg_modifiers "$cfg" "$@"
            ;;
    esac

    # Support <cfg>-p0 etc modifiers to use only one port
    if test "${cfg}" != "${cfg%-p[0-9]}" ; then
        run_conf="${cfg%-p[0-9]}"
        # VF modifiers must be applied after port modifiers
        MOD_OPTS=(--script=scripts/only-"${cfg#${run_conf}-}" "${MOD_OPTS[@]}")
        cfg="${run_conf}"
    fi
    # Modifier must be applied after base configuration
    RUN_OPTS+=(--opts=run/"${cfg}" "${MOD_OPTS[@]}")
}

# In ts-conf/env/sfc do not try to autodetect interface names for hosts -
# it is not possible here because sfc driver is loaded only by prologue.
# Instead specify interfaces in terms of PCI.
export TE_SFC_IFS_BY_PCI=yes

declare -a RUN_OPTS
declare -a GEN_OPTS

while test -n "$1" ; do

    if call_if_defined grab_cfg_check_opt "$1" ; then
        shift 1
        continue
    fi

    case "$1" in
        --help) usage ;;

        --cfg=*)
            test -z "${CFG}" ||
              run_fail "Configuration is specified twice: ${CFG} vs ${1#--cfg=}"

            CFG="${1#--cfg=}"
            ;;

        --reuse-pco)
            export TE_ENV_REUSE_PCO=yes
            ;;

        --phy-autoneg=*)
            export NET_DRV_TS_IUT_PHY_AUTONEG="${1#--phy-autoneg=}"
            export NET_DRV_TS_TST1_PHY_AUTONEG="${1#--phy-autoneg=}"
            ;;

        --phy-duplex=*)
            export NET_DRV_TS_IUT_PHY_DUPLEX="${1#--phy-duplex=}"
            export NET_DRV_TS_TST1_PHY_DUPLEX="${1#--phy-duplex=}"
            ;;

        --phy-speed=*)
            export NET_DRV_TS_IUT_PHY_SPEED="${1#--phy-speed=}"
            export NET_DRV_TS_TST1_PHY_SPEED="${1#--phy-speed=}"
            ;;

        --iut-phy-autoneg=*)
            export NET_DRV_TS_IUT_PHY_AUTONEG="${1#--iut-phy-autoneg=}"
            ;;

        --iut-phy-duplex=*)
            export NET_DRV_TS_IUT_PHY_DUPLEX="${1#--iut-phy-duplex=}"
            ;;

        --iut-phy-speed=*)
            export NET_DRV_TS_IUT_PHY_SPEED="${1#--iut-phy-speed=}"
            ;;

        --tst-phy-autoneg=*)
            export NET_DRV_TS_TST1_PHY_AUTONEG="${1#--tst-phy-autoneg=}"
            ;;

        --tst-phy-duplex=*)
            export NET_DRV_TS_TST1_PHY_DUPLEX="${1#--tst-phy-duplex=}"
            ;;

        --tst-phy-speed=*)
            export NET_DRV_TS_TST1_PHY_SPEED="${1#--tst-phy-speed=}"
            ;;

        --net-driver-ndebug)
            NET_DRIVER_MAKE_ARGS+=" NDEBUG=1"
            export NET_DRIVER_MAKE_ARGS
            ;;

        --no-meta)
            RUN_OPTS+=("$1")
            TE_RUN_META=no
            ;;

        --publish)
            source "${TE_TS_RIGSDIR}/scripts/publish_logs/ts_publish"
            pscript="$(tsrigs_publish_get net-drv-ts)"
            RUN_OPTS+=("--publish=${pscript}")
            ;;

        *)  RUN_OPTS+=("$1") ;;
    esac
    shift 1
done

if test -n "${CFG}" ; then
    IFS=: ; process_cfg ${CFG} ; IFS=
fi

if [[ "${TE_RUN_META}" = "yes" ]] ; then
    te_meta_test_suite "net-drv-ts"

    te_meta_set CFG "${CFG}"
    te_meta_set_git "${SF_TS_CONFDIR}" TSCONF
fi

RUN_OPTS+=(--opts=opts.ts)

conf_dirs="${TE_TS_CONFDIR}"
if [[ -n "${TE_TS_RIGSDIR}" ]] ; then
    conf_dirs="${conf_dirs}:${TE_TS_RIGSDIR}"
fi
conf_dirs="${conf_dirs}:${SF_TS_CONFDIR}"
GEN_OPTS+=(--conf-dirs="${conf_dirs}")

GEN_OPTS+=(--trc-db="${TE_TS_TRC_DB}")
[[ -z "${TE_TS_RIGS_TRC_DB}" ]] || GEN_OPTS+=(--trc-db="${TE_TS_RIGS_TRC_DB}")
GEN_OPTS+=(--trc-comparison=normalised)
GEN_OPTS+=(--trc-html=trc-brief.html)
GEN_OPTS+=(--trc-no-expected)
GEN_OPTS+=(--trc-no-total --trc-no-unspec)
GEN_OPTS+=(--tester-only-req-logues)

if [[ -z "${TE_ENV_SFPTPD}" ]] ; then
    # Path to sfptpd is not exported
    RUN_OPTS+=("--tester-req=!SFPTPD")
fi

"${TE_BASE}"/dispatcher.sh "${GEN_OPTS[@]}" "${RUN_OPTS[@]}"
RESULT=$?

if test ${RESULT} -ne 0 ; then
    echo FAIL
    echo ""
fi

echo -ne "\a"
exit ${RESULT}
