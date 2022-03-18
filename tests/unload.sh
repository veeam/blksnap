#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

modprobe -r blksnap

echo 0 > /sys/kernel/livepatch/bdevfilter/enabled
sleep 3s
modprobe -r bdevfilter
