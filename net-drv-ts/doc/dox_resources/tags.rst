..
   SPDX-License-Identifier: Apache-2.0
   (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

.. index:: pair: group; Tags
.. _tags_details:

TRC Tags List
=============

List of tags set on various testing configurations to distinguish them.

.. list-table::
  :header-rows: 1

  *
    - Name
    - Description
    - Example
  *
    - *DRIVER*
    - Driver name. Typically it is the first choice if it is not known
      that the driver supports the feature, but HW does not have it.
    - i40e, ice, sfc
  *
    - linux-mm:*VALUE*
    - Linux kernel major and minor versions combined in single integer
      to classify testing results which depend on it. May be used for
      comparison: greater, equal, less.
    - linux-mm:510
  *
    - max_rx_queues:*VALUE*
    - Number of receive queues.
      May be used for comparison: greater, equal, less.
    - max_rx_queues:8
  *
    - max_tx_queues:*VALUE*
    - Number of transmit queues.
      May be used for comparison: greater, equal, less.
    - max_tx_queues:8
  *
    - pci-*VENDOR_ID*
    - PCI device vendor ID to classify vendor-dependent testing results
    - pci-1af4
  *
    - pci-*VENDOR_ID*-*DEVICE_ID*
    - PCI vendor and device IDs to classify HW-dependent testing results
    - pci-1af4-1000
  *
    - pci-sub-*VENDOR_ID*
    - PCI subsystem vendor ID to classify vendor-dependent testing results
    - pci-1af4
  *
    - pci-sub-*VENDOR_ID*-*DEVICE_ID*
    - PCI subsystem vendor and device IDs to classify HW-dependent testing
      results
    - pci-1af4-1000
  *
    - peer-*DRIVER*
    - Name of the driver used on peer (aka Tester).
    - peer-i40e
  *
    - no-ethtool-opt-*
    - ethtool CLI option is not present in `ethtool --help` output on IUT.
    - no-ethtool-opt-show-fec
  *
    - *SIDE*-no-tun
    - TUN support is unavailable on IUT or Tester (`/dev/net/tun` cannot be opened on the corresponding side).
    - iut-no-tun
  *
    - *SIDE*-cpus:*VALUE*
    - Number of CPUs on IUT or Tester side.
      May be used for comparison: greater, equal, less.
    - iut-cpus:2
  *
    - *SIDE*-no-ptp
    - PTP kernel interface is unavailable on IUT or Tester side.
    - iut-no-ptp
  *
    - pf-count:*VALUE*
    - Number of network physical functions in the same PCI device as IUT interface.
      May be used for comparison: greater, equal, less.
    - pf-count:2
  *
    - active-port-count:*VALUE*
    - Number of active network ports in physical functions from the same as IUT interface.
      May be used for comparison: greater, equal, less.
    - active-port-count:2
