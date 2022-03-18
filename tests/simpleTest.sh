#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0+

modprobe veeamsnap

SRC_DEVICE=$1
TEST_MOUNT_DIR="/mnt/snap_test"
SRC_MOUNT_DIR="$TEST_MOUNT_DIR/src"
DST_MOUNT_DIR="$TEST_MOUNT_DIR/dst"

mkdir -p "$TEST_MOUNT_DIR"
mkdir -p "$SRC_MOUNT_DIR"
mkdir -p "$DST_MOUNT_DIR"

mount $1 $SRC_MOUNT_DIR
touch "$SRC_MOUNT_DIR/test_file"
dd if=/dev/urandom of=$SRC_MOUNT_DIR/test_file bs=1M count=1 > /dev/null
ORIGINAL_SHA=$(sha1sum "$SRC_MOUNT_DIR/test_file")

#take snapshot
echo "Take snapshot from $SRC_DEVICE"
STORE_UUID=$(./blksnap create-in-memory-store --snap-dev $SRC_DEVICE)
echo "STORE_UUID: $STORE_UUID"
SNAP_ID=$(./blksnap create-snapshot --device $SRC_DEVICE --store $STORE_UUID)
echo "SNAP_ID: $SNAP_ID"

mount "/dev/veeamimage0" $DST_MOUNT_DIR
SNAPSHOT_SHA=$(sha1sum "$DST_MOUNT_DIR/test_file")
echo "ORIGINAL_SHA: $ORIGINAL_SHA"
echo "SNAPSHOT_SHA: $SNAPSHOT_SHA"

echo ""
echo "Change original data"
dd if=/dev/urandom of=$SRC_MOUNT_DIR/test_file bs=1M count=1 > /dev/null
ORIGINAL_SHA=$(sha1sum "$SRC_MOUNT_DIR/test_file")
SNAPSHOT_SHA=$(sha1sum "$DST_MOUNT_DIR/test_file")
echo "ORIGINAL_SHA: $ORIGINAL_SHA"
echo "SNAPSHOT_SHA: $SNAPSHOT_SHA"

