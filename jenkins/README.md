[SPDX-License-Identifier: Apache-2.0]::
[Copyright (C) 2023-2026 OKTET Labs Ltd. All rights reserved.]::

# Jenkins pipelines

The code here can be used to organize automated testing via Jenkins.

It uses TE Jenkins library (see `README.md` in `te-jenkins` repository) and
also assumes that `ts-rigs` repository is available and contains some site
specific information.

## Required ts-rigs content

`ts-rigs` should contain `jenkins/defs/net-drv-ts/defs.groovy` file which
sets some test suite specific variables in `set_defs()` method. Example:

```
def set_defs(ctx) {
    ctx.TS_GIT_URL = "https://github.com/Xilinx-CNS/cns-net-drv-ts"
    ctx.TS_MAIL_FROM = 'a.b@foobar.com'
    ctx.TS_MAIL_TO = 'c.d@foobar.com'

    // Variables required for publish-logs
    ctx.TS_LOGS_SUBPATH = 'testing/logs/net-drv-ts/'

    // Variables required for bublik-import
    ctx.PROJECT = 'project_name'
    ctx.TS_BUBLIK_URL = 'https://foobar.com/bublik/'
    ctx.TS_LOGS_URL_PREFIX = 'https://foobar.com/someprefix/'

    // Log listener used by Bublik
    ctx.TS_LOG_LISTENER_NAME = 'somelistener'
}

return this
```

See `README.md` in `te-jenkins` for more information about these variables.

## How to configure pipelines in Jenkins

See `README.md` in `te-jenkins`, there you can find generic information
about configuring Jenkins and creating pipelines.

Specification for the following pipelines is available here:
- `update` - pipeline for rebulding sources after detecting new commits
  in used repositories (including this one and TE). Based on `teTsPipeline`
  template in `te-jenkins`.
  Node with label `net-drv-ts` should be configured in Jenkins where
  this pipeline will be run. This test suite will be built there.
- `doc` - pipeline for building documentation for this test suite, based on
  `teTsDoc` template.
  Node with label `net-drv-ts-doc` should be configured in Jenkins where
  this pipeline will be run.
- `run` - pipeline for running tests and publishing testing logs, based
  on `teTsPipeline` template.
  Node with label `net-drv-ts` should be configured in Jenkins where
  this pipeline will be run. This test suite will be built and run there.

## Building external driver sources

`update` and `run` can checkout external driver sources and export
`TE_IUT_NET_DRV_SRC`.

Typical flow:

- driver build job publishes driver revisions;
- `update` gets revisions from that job via `get_revs_from`;
- `run` gets revisions from `update`.

Driver may be taken from revision name.
For example, if loaded revisions contain driver name `somename`, helper uses
`SOMENAME_GIT_URL` and `SOMENAME_REV`/`SOMENAME_BRANCH`.

For names with non-alphanumeric characters (for example `some-name`),
helper first tries normalized keys where such characters are replaced with
`_` and the name is uppercased (`SOME_NAME_*`).
Raw keys from revisions (`SOME-NAME_*`) are also accepted.

If loaded revisions contain multiple non-core names
(`te`, `ts`, `tsconf`, `tsrigs` are ignored), choose one via optional
`net_drv_name` job parameter (or `NET_DRV_NAME` in site-specific defs).

`net_drv_repo` and `net_drv_rev` parameters in `run` / `update` can override
URL/revision.

If no driver name is found, integration is skipped.

`TE_IUT_NET_DRV_BUILD` may be set directly in
`jenkins/defs/net-drv-ts/defs.groovy`. Alternatively,
`NET_DRV_BUILD_SCRIPTS` may be set there as a map with driver
name used as a key.
Absolute paths are used as is, relative paths are resolved against
`TSRIGS_SRC`.
