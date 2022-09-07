#! /bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.
#
# Generate TRC status reports.
#
# Author: Andrew Rybchenko <andrew.rybchenko@oktetlabs.ru>
#

source "$(dirname "$(which $0)")"/guess.sh

declare -a COMMON_OPTS
COMMON_OPTS+=(--db="${TE_TS_TRC_DB}")
COMMON_OPTS+=(--key2html="${SF_TS_CONFDIR}"/trc.key2html)

declare -a COMMON_TAGS

declare -a REF_OPTS

for tag in "${COMMON_TAGS[@]}" ; do
    REF_OPTS+=(-1 "${tag}")
done


#
# X2 sfc driver status
#

declare -a X2_SFC_TAGS
X2_SFC_TAGS+=(sfc)
X2_SFC_TAGS+=(pci-1924 pci-1924-0b03)

declare -a X2_SFC_OPTS2

for tag in "${COMMON_TAGS[@]}" ; do
    X2_SFC_OPTS2+=(-2 "${tag}")
done
for tag in "${X2_SFC_TAGS[@]}" ; do
    X2_SFC_OPTS2+=(-2 "${tag}")
done

declare -a X2_SFC_STATUS_OPTS
X2_SFC_STATUS_OPTS+=("${COMMON_OPTS[@]}")
X2_SFC_STATUS_OPTS+=(--1-name="Reference")
X2_SFC_STATUS_OPTS+=("${REF_OPTS[@]}")
X2_SFC_STATUS_OPTS+=(--2-name="X2 sfc")
X2_SFC_STATUS_OPTS+=(--2-show-keys)
X2_SFC_STATUS_OPTS+=("${X2_SFC_OPTS2[@]}")

te-trc-diff \
    --html=X2-sfc.html \
    --title="X2 sfc driver status" \
    "${X2_SFC_STATUS_OPTS[@]}"


#
# X3 xilinx_efct driver status
#

declare -a X3_XLNX_EFCT_TAGS
X3_XLNX_EFCT_TAGS+=(xilinx_efct)
X3_XLNX_EFCT_TAGS+=(pci-10ee pci-10ee-5084)

declare -a X3_XLNX_EFCT_OPTS2

for tag in "${COMMON_TAGS[@]}" ; do
    X3_XLNX_EFCT_OPTS2+=(-2 "${tag}")
done
for tag in "${X3_XLNX_EFCT_TAGS[@]}" ; do
    X3_XLNX_EFCT_OPTS2+=(-2 "${tag}")
done

declare -a X3_XLNX_EFCT_STATUS_OPTS
X3_XLNX_EFCT_STATUS_OPTS+=("${COMMON_OPTS[@]}")
X3_XLNX_EFCT_STATUS_OPTS+=(--1-name="Reference")
X3_XLNX_EFCT_STATUS_OPTS+=("${REF_OPTS[@]}")
X3_XLNX_EFCT_STATUS_OPTS+=(--2-name="X3 xilinx_efct")
X3_XLNX_EFCT_STATUS_OPTS+=(--2-show-keys)
X3_XLNX_EFCT_STATUS_OPTS+=("${X3_XLNX_EFCT_OPTS2[@]}")

te-trc-diff \
    --html=X3-xilinx_efct.html \
    --title="X3 xilinx_efct driver status" \
    "${X3_XLNX_EFCT_STATUS_OPTS[@]}"


#
# SN1022 sfc_ef100 driver status
#

declare -a SN1022_SFC_EF100_TAGS
SN1022_SFC_EF100_TAGS+=(sfc_ef100)
SN1022_SFC_EF100_TAGS+=(pci-10ee pci-10ee-0100)

declare -a SN1022_SFC_EF100_OPTS2

for tag in "${COMMON_TAGS[@]}" ; do
    SN1022_SFC_EF100_OPTS2+=(-2 "${tag}")
done
for tag in "${SN1022_SFC_EF100_TAGS[@]}" ; do
    SN1022_SFC_EF100_OPTS2+=(-2 "${tag}")
done

declare -a SN1022_EF100_STATUS_OPTS
SN1022_EF100_STATUS_OPTS+=("${COMMON_OPTS[@]}")
SN1022_EF100_STATUS_OPTS+=(--1-name="Reference")
SN1022_EF100_STATUS_OPTS+=("${REF_OPTS[@]}")
SN1022_EF100_STATUS_OPTS+=(--2-name="SN1022 sfc_ef100")
SN1022_EF100_STATUS_OPTS+=(--2-show-keys)
SN1022_EF100_STATUS_OPTS+=("${SN1022_SFC_EF100_OPTS2[@]}")

te-trc-diff \
    --html=SN1022-sfc_ef100.html \
    --title="SN1022 sfc_ef100 driver status" \
    "${SN1022_EF100_STATUS_OPTS[@]}"


#
# Generic Virtio-Net status
#

declare -a VIRTIO_NET_TAGS
VIRTIO_NET_TAGS+=(virtio-pci)
VIRTIO_NET_TAGS+=(pci-1af4-1000 pci-sub-1af4-0001)

declare -a VIRTIO_NET_OPTS2

for tag in "${VIRTIO_NET_TAGS[@]}" ; do
    VIRTIO_NET_OPTS2+=(-2 "${tag}")
done

declare -a VNET_STATUS_OPTS
VNET_STATUS_OPTS+=("${COMMON_OPTS[@]}")
VNET_STATUS_OPTS+=(--1-name="Reference")
VNET_STATUS_OPTS+=("${REF_OPTS[@]}")
VNET_STATUS_OPTS+=(--2-name="Virtio-Net")
VNET_STATUS_OPTS+=(--2-show-keys)
VNET_STATUS_OPTS+=("${VIRTIO_NET_OPTS2[@]}")

te-trc-diff \
    --html=Virtio-Net.html \
    --title="Virtio-Net status" \
    "${VNET_STATUS_OPTS[@]}"


#
# SN1022 Virtio-Net status
#

declare -a SN1022_VIRTIO_NET_TAGS
SN1022_VIRTIO_NET_TAGS+=(xilinx-virtio-net)

declare -a VIRTIO_NET_OPTS1
declare -a SN1022_VIRTIO_NET_OPTS2

for tag in "${VIRTIO_NET_TAGS[@]}" ; do
    VIRTIO_NET_OPTS1+=(-1 "${tag}")
    SN1022_VIRTIO_NET_OPTS2+=(-2 "${tag}")
done

for tag in "${SN1022_VIRTIO_NET_TAGS[@]}" ; do
    SN1022_VIRTIO_NET_OPTS2+=(-2 "${tag}")
done

declare -a SN1022_VNET_STATUS_OPTS
SN1022_VNET_STATUS_OPTS+=("${COMMON_OPTS[@]}")
SN1022_VNET_STATUS_OPTS+=(--1-name="Virtio-Net")
SN1022_VNET_STATUS_OPTS+=("${VIRTIO_NET_OPTS1[@]}")
SN1022_VNET_STATUS_OPTS+=(--2-name="SN1022 Virtio-Net")
SN1022_VNET_STATUS_OPTS+=(--2-show-keys)
SN1022_VNET_STATUS_OPTS+=("${SN1022_VIRTIO_NET_OPTS2[@]}")

te-trc-diff \
    --html=SN1022-Virtio-Net.html \
    --title="SN1022 Virtio-Net status" \
    "${SN1022_VNET_STATUS_OPTS[@]}"