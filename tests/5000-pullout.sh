#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "pullout test start"

blksnap_load

# check module is ready
blksnap_version

TESTDIR=/tmp/blksnap-test
rm -rf ${TESTDIR}
mkdir -p ${TESTDIR}

MPDIR=/mnt/blksnap-test
rm -rf ${MPDIR}
mkdir -p ${MPDIR}

modprobe zram num_devices=2 && sleep 1

# create first device
DEVICE_1="/dev/zram0"
zramctl --size 128M --algorithm lz4 ${DEVICE_1}
mkfs.ext4 ${DEVICE_1}
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

# create second device
DEVICE_2="/dev/zram1"
zramctl --size 128M --algorithm lz4 ${DEVICE_2}
mkfs.ext4 ${DEVICE_2}
echo "new device ${DEVICE_2}"

MOUNTPOINT_2=${MPDIR}/simple_2
mkdir -p ${MOUNTPOINT_2}
mount ${DEVICE_2} ${MOUNTPOINT_2}

generate_files ${MOUNTPOINT_1} "before" 9
drop_cache

echo "Block device prepared"
#echo "press ..."
#read -n 1

blksnap_snapshot_create "${DEVICE_1} ${DEVICE_2}"

DIFF_STORAGE=~/diff_storage0
rm -f ${DIFF_STORAGE}
fallocate --length 1GiB ${DIFF_STORAGE}
blksnap_snapshot_appendstorage ${DIFF_STORAGE}

blksnap_snapshot_take

echo "Snapshot was token"
#echo "press ..."
#read -n 1

blksnap_snapshot_collect_all

echo "Write to original"
#echo "Write something" > ${MOUNTPOINT_1}/something.txt
generate_files ${MOUNTPOINT_1} "after" 3
drop_cache

check_files ${MOUNTPOINT_1}

echo "Check snapshots"
IMAGE_1=${TESTDIR}/image0
mkdir -p ${IMAGE_1}
mount /dev/veeamblksnap-image0 ${IMAGE_1}
check_files ${IMAGE_1}

echo "Write to snapshot"
generate_files ${IMAGE_1} "snapshot" 3

drop_cache
umount /dev/veeamblksnap-image0
mount /dev/veeamblksnap-image0 ${IMAGE_1}

check_files ${IMAGE_1}

umount ${IMAGE_1}

blksnap_snapshot_destroy

echo "Destroy snapshot"
#echo "press ..."
#read -n 1

rm ${DIFF_STORAGE}

drop_cache
umount ${DEVICE_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

check_files ${MOUNTPOINT_1}

echo "Destroy devices"
umount ${MOUNTPOINT_2}
umount ${MOUNTPOINT_1}
modprobe -r zram

echo "Tracking device info:"
blksnap_tracker_collect

blksnap_unload

echo "pullout test finish"
echo "---"
