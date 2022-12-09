.. SPDX-License-Identifier: GPL-2.0

================================
Block Device Filtering Mechanism
================================

The block device filtering mechanism is an API that allows to attach block device filters.
Block device filters allow perform additional processing for I/O units.

Introduction
============

The idea of handling I/O units on block devices is not new.
Back in the 2.6 kernel, there was an undocumented possibility of handling I/O units by substituting the make_request_fn() function, which belonged to the request_queue structure.
But no kernel module used this feature, and it was eliminated in the 5.10 kernel.

The block device filtering mechanism returns the ability to handle I/O units.
It is possible to safely attach filter to a block device "on the fly" without changing the structure of block devices.

It supports attaching one filter to one block device, because there is only one filter implementation in the kernel.
See Documentation/block/blksnap.rst.

Design
======

The block device filtering mechanism provides functions for attaching and detaching the filter.
The filter is a structure with a reference counter and callback functions.

The submit_bio_cb() callback function is called for each I/O unit for a block device, providing I/O unit filtering.
Depending on the result of filtering the I/O unit, it can either be passed for subsequent processing by the block layer, or skipped.

The reference counter allows to control the filter lifetime.
When the reference count is reduced to zero, the release_cb() callback function is called to release the filter.
This allows the filter to be released when the block device is disconnected.

Interface description
=====================
.. kernel-doc:: include/linux/blkdev.h
	:functions: bdev_filter_operations bdev_filter bdev_filter_init bdev_filter_get bdev_filter_put
.. kernel-doc:: block/bdev.c
	:functions: bdev_filter_attach bdev_filter_detach
