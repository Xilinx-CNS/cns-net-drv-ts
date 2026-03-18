// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 OKTET Labs Ltd. All rights reserved.
//
// Helper for checking out external driver sources for net-drv-ts Jenkins jobs.

def getDriverId(ctx, String name) {
    def raw = (name ?: '').toString().trim()

    if (!raw) {
        return ''
    }

    return ctx.teCommon.str2id(raw).toLowerCase()
}

def getDriverMetaValue(ctx, String drvName, List suffixes, Map vars = null) {
    def prefixes = []
    def rawName = (drvName ?: '').toString().trim()
    def drvId = getDriverId(ctx, rawName)

    if (drvId) {
        prefixes += "${drvId.toUpperCase()}_"
    }

    if (rawName) {
        prefixes += "${rawName.toUpperCase()}_"
    }

    for (String prefix : prefixes.unique()) {
        for (String suffix : suffixes) {
            def varName = "${prefix}${suffix}"
            def value = (ctx[varName] ?:
                         ctx.env[varName] ?:
                         vars?.get(varName) ?: '')
                        .toString().trim()
            if (value) {
                return value
            }
        }
    }

    return ''
}

def getDriverName(ctx) {
    def coreComponents = ['te', 'ts', 'tsconf', 'tsrigs'] as Set
    def candidates = []
    def allRevs = ctx.all_revs
    def selected = (ctx.params.net_drv_name ?:
                    ctx.NET_DRV_NAME ?:
                    ctx.env.NET_DRV_NAME ?: '').toString().trim()

    if (allRevs instanceof Map) {
        allRevs.each {
            name, vars ->
            def drvName = name?.toString()?.trim()
            def drvId

            if (!drvName) {
                return
            }

            drvId = getDriverId(ctx, drvName)
            if (coreComponents.contains(drvId)) {
                return
            }

            def hasAny = getDriverMetaValue(ctx, drvName,
                                            ['GIT_URL', 'REV', 'BRANCH'], vars)

            if (!hasAny) {
                return
            }
            candidates += drvId
        }
    }

    candidates = candidates.unique()

    if (selected) {
        selected = getDriverId(ctx, selected)
        if (!candidates.isEmpty() && !candidates.contains(selected)) {
            error("NET_DRV_NAME=${selected} is not found in loaded revisions; found: ${candidates.join(', ')}")
        }
        return selected
    }

    if (candidates.size() > 1) {
        error("Cannot determine driver name from loaded revisions; found: ${candidates.join(', ')}. Set net_drv_name parameter (or NET_DRV_NAME) to pick one.")
    }

    if (!candidates.isEmpty()) {
        return candidates[0]
    }

    return null
}

def setDriverBuildScript(ctx) {
    def drvName = getDriverName(ctx)

    if (!drvName) {
        return
    }

    if (ctx.TE_IUT_NET_DRV_BUILD ?: ctx.env.TE_IUT_NET_DRV_BUILD) {
        return
    }

    def scripts = ctx.NET_DRV_BUILD_SCRIPTS
    if (!(scripts instanceof Map)) {
        return
    }

    def entry = scripts[drvName]
    if (entry == null) {
        entry = scripts[getDriverId(ctx, drvName)]
    }
    if (entry == null) {
        return
    }

    def build = entry.toString().trim()

    if (!build) {
        return
    }

    if (!build.startsWith('/')) {
        def tsrigsSrc = (ctx.TSRIGS_SRC ?: '').toString().trim()

        if (!tsrigsSrc) {
            error("TSRIGS_SRC is not defined for ${drvName}")
        }

        build = "${tsrigsSrc}/${build.replaceFirst('^\\./', '')}"
    }

    ctx.TE_IUT_NET_DRV_BUILD = build
    ctx.env.TE_IUT_NET_DRV_BUILD = build
}

def checkoutDriverSources(ctx) {
    def drvName = getDriverName(ctx)

    if (!drvName) {
        return
    }

    def componentId = ctx.teCommon.str2id(drvName).toLowerCase()
    def varPrefix = "${componentId}_".toUpperCase()

    if (ctx.TE_IUT_NET_DRV_SRC ?: ctx.env.TE_IUT_NET_DRV_SRC) {
        return
    }

    def url = (ctx.params.net_drv_repo ?:
               getDriverMetaValue(ctx, drvName, ['GIT_URL']) ?: '')
              .toString().trim()
    def revision = (ctx.params.net_drv_rev ?:
                    getDriverMetaValue(ctx, drvName, ['REV', 'BRANCH']) ?: '')
                   .toString().trim()

    if (!url) {
        error("Repository URL is not defined for ${drvName}")
    }

    if (!revision) {
        error("Revision is not defined for ${drvName}")
    }

    ctx.teRun.generic_checkout(ctx: ctx,
                               component: componentId,
                               url: url,
                               revision: revision,
                               target: componentId,
                               do_poll: false)

    def srcVar = "${varPrefix}SRC"
    if (ctx[srcVar]) {
        ctx.TE_IUT_NET_DRV_SRC = ctx[srcVar]
        ctx.env.TE_IUT_NET_DRV_SRC = ctx[srcVar]
    } else {
        error("Driver sources path is not defined for ${drvName}")
    }
}

return this
