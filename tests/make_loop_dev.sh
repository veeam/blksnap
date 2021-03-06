#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0+

set -e
DEVICE_PATH="/tmp/loopbackfile.img"
#echo "Prepare device $DEVICE_PATH"

#echo "fill zero"
dd if=/dev/zero of=$DEVICE_PATH bs=1G count=1

#echo "mkfs"
mkfs.ext4 $DEVICE_PATH

LOOP_PATH=$(losetup -f --show $DEVICE_PATH)
echo $LOOP_PATH
