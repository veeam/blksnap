#!/bin/bash -e

. ./functions.sh
. ./blksnap.sh

modprobe blk-snap
sleep 2s

echo "---"
echo "Simple test start"

# check module is ready
blksnap_version

TESTDIR=/tmp/blk-snap-test
mkdir -p ${TESTDIR}

MPDIR=/mnt/blk-snap-test
mkdir -p ${MPDIR}

# create first device
IMAGEFILE_1=${TESTDIR}/simple_1.img
imagefile_make ${IMAGEFILE_1} 64
echo "new image file ${IMAGEFILE_1}"

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1})
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

# create second device
IMAGEFILE_2=${TESTDIR}/simple_2.img
imagefile_make ${IMAGEFILE_2} 128
echo "new image file ${IMAGEFILE_2}"

DEVICE_2=$(loop_device_attach ${IMAGEFILE_2})
echo "new device ${DEVICE_2}"

MOUNTPOINT_2=${MPDIR}/simple_2
mkdir -p ${MOUNTPOINT_2}
mount ${DEVICE_2} ${MOUNTPOINT_2}

generate_files ${MOUNTPOINT_1} "before" 9
drop_cache

echo "Block device prepared, press ..."
#read -n 1

blksnap_snapshot_create "${DEVICE_1} ${DEVICE_2}"

DIFF_STORAGE=~/diff_storage0
fallocate --length 1GiB ${DIFF_STORAGE}
blksnap_snapshot_append ${DIFF_STORAGE}

blksnap_snapshot_take

echo "Snapshot was token, press ..."
read -n 1

blksnap_snapshot_collect

echo "Write to original"
#echo "Write something" > ${MOUNTPOINT_1}/something.txt
generate_files ${MOUNTPOINT_1} "after" 3
drop_cache

check_files ${MOUNTPOINT_1}

echo "Check snapshots"
IMAGE_1=${TESTDIR}/image0
mkdir -p ${IMAGE_1}
mount /dev/blk-snap-image0 ${IMAGE_1}
check_files ${IMAGE_1}

echo "Write to snapshot"
generate_files ${IMAGE_1} "snapshot" 3

drop_cache
umount /dev/blk-snap-image0
mount /dev/blk-snap-image0 ${IMAGE_1}

check_files ${IMAGE_1}

umount ${IMAGE_1}

#dd if=/dev/blk-snap-image0 of=${TESTDIR}/image0 bs=1M
#generate_files ${MOUNTPOINT_1} "after" 3
#dd if=/dev/blk-snap-image0 of=${TESTDIR}/image0 bs=1M

#dd if=/dev/blk-snap-image1 of=${TESTDIR}/image1 bs=1M
#dd if=/dev/blk-snap-image0 of=${TESTDIR}/image0 bs=4096 count=1
#dd if=/dev/blk-snap-image1 of=${TESTDIR}/image1 bs=4096 count=1

blksnap_snapshot_destroy

echo "Destroy snapshot, press ..."
#read -n 1

rm ${DIFF_STORAGE}

drop_cache
umount ${DEVICE_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

check_files ${MOUNTPOINT_1}

echo "Destroy second device"
umount ${MOUNTPOINT_2}
loop_device_detach ${DEVICE_2}
imagefile_cleanup ${IMAGEFILE_2}

echo "Destroy first device"
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

echo "Tracking device info:"
blksnap_tracker_collect

echo "Simple test finish"
echo "---"

echo 0 > /sys/kernel/livepatch/blk_snap/enabled
sleep 2s
modprobe -r blk-snap
