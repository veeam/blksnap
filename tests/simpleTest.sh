#!/bin/bash
# [TBD]
# Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
#
# This file is part of blksnap-tests
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

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

