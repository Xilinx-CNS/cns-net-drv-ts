#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

source "$(dirname "$(which "$0")")"/guess.sh

declare -a OPTS
OPTS+=(--db="${TE_TS_TRC_DB}")
if [[ -n "${TE_TS_RIGSDIR}" ]] \
   && [[ -r "${TE_TS_RIGSDIR}"/trc.key2html ]] ; then
    OPTS+=(--key2html="${TE_TS_RIGSDIR}"/trc.key2html)
fi

. $TE_BASE/scripts/trc.sh "${OPTS[@]}" "$@"
