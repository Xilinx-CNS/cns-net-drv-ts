..
   SPDX-License-Identifier: Apache-2.0
   (c) Copyright 2021 - 2025 Xilinx, Inc. All rights reserved

.. index:: pair: group; Introduction
.. _introduction:

Introduction
============

Net Driver Test Suite is aimed to check certain implementation of an
Ethernet NIC driver. It uses TE as its engine.

See :ref:`Abbreviations<term_abbrev>`

References
----------

Find list of documentation mentioned on the
:ref:`References page<refs_details>`

Conventions
-----------

Parameters of tests are referred in the text using bold italic (for example,
**iut_rpcs**).

Standard defines, elements of enumerations as well as **errno** values
appear in monotype font (for example, `AF_INET`, `ENOMEM`).

Testbed
-------

The following setup is required to run the test suite. In theory, Test Engine
may run on either Test machine or Auxiliary test machine.

.. image:: images/testbed.png
  :alt: Testbed Topology

Test parameters
---------------

The test suite uses extensive parametrization. Details of the XML syntax and
how to read the XML that describes the parameters can be found in
`TE Tester`_
documentation.

Parameter naming conventions
****************************

.. list-table::
  :header-rows: 1

  *
    - Name
    - Description
  *
    - iut_rpcs
    - RPC server on IUT
  *
    - iut_if
    - Network interface on the IUT
  *
    - iut_mac
    - MAC address of the **iut_if**
  *
    - iut_addr
    - IPv4/6 unicast address allocated for IUT interface
  *
    - tst_rpcs
    - RPC server on TST
  *
    - tst_if
    - Network interface on TST connected to the **iut_if**
  *
    - tst_mac
    - MAC address of the **tst_if**
  *
    - tst_addr
    - IPv4/6 unicast address assigned to the **tst_if**
  *
    - other_mac
    - Other unicast MAC address


Test Suite Structure
--------------------

* :ref:`Tests <doxid-group__net__drv__tests>`

********
TRC Tags
********

List of TRC tags can be found on the
:ref:`corresponding page<tags_details>`.

********************
Tester Requirements
********************

List of Tester requirements can be found on the
:ref:`corresponding page<reqs_details>`.

