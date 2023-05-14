[SPDX-License-Identifier: Apache-2.0]::
[Copyright (C) 2023 OKTET Labs Ltd. All rights reserved.]::

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
