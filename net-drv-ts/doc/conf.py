# -*- coding: utf-8 -*-
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

project = u'Net Driver Test Suite'
copyright = u'(c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.'
author = u'Dmitry Izbitsky'
version = u'1.0'
release = u'release'
latex_documents = [
    (
        'index',
        'net_driver_test_suite.tex',
        u'Net Driver Test Suite',
        u'Dmitry Izbitsky',
        'manual',
    ),
]

# Specify the path to Doxyrest extensions for Sphinx:

import sys
import os
doxyrest_path = os.getenv('DOXYREST_PREFIX')
sys.path.insert(1, os.path.abspath(doxyrest_path + '/share/doxyrest/sphinx'))

source_suffix = '.rst'
master_doc = 'index'
language = "c"
# Configure exclude patterns based on builder type
def setup(app):
    def on_builder_inited(app):
        if app.builder.name != 'html':
            # Exclude global.rst for non-HTML builders (like latex/pdf)
            app.config.exclude_patterns.append('generated/rst/global.rst')
            app.config.exclude_patterns.append('generated/rst/enum*.rst')
            app.config.exclude_patterns.append('generated/rst/struct*.rst')


    app.connect('builder-inited', on_builder_inited)

exclude_patterns = [
    'generated/rst/index.rst',
]

import sphinx_rtd_theme
extensions = [
    "sphinx_rtd_theme"
]
html_theme = 'sphinx_rtd_theme'

html_theme_options = {
    'collapse_navigation': False,
}

extensions += ['doxyrest', 'cpplexer']

