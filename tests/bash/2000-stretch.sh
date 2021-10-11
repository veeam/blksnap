#!/bin/bash -e

. ./functions.sh
. ./blksnap.sh


# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB dikk_storage portion size
modprobe blk-snap diff_storage_minimum=262144
sleep 2s

echo "---"
echo "Stretch snapshot test"

# check module is ready
blksnap_version

TESTDIR=~/blk-snap-test
MPDIR=/mnt/blk-snap-test
DIFF_STORAGE=~/diff_storage/
mkdir -p ${TESTDIR}
mkdir -p ${MPDIR}
mkdir -p ${DIFF_STORAGE}

# create first device
IMAGEFILE_1=${TESTDIR}/simple_1.img
imagefile_make ${IMAGEFILE_1} 4096
echo "new image file ${IMAGEFILE_1}"

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1})
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

generate_files ${MOUNTPOINT_1} "before" 5
drop_cache

blksnap_snapshot_create "${DEVICE_1}"

generate_files ${MOUNTPOINT_1} "tracked" 5
drop_cache

fallocate --length 256MiB "${DIFF_STORAGE}/diff_storage"
blksnap_snapshot_append "${DIFF_STORAGE}/diff_storage"

echo "Call: ${BLKSNAP} stretch_snapshot --id=${ID} --path=${DIFF_STORAGE} --limit=1024"
echo "Press for taking snapshot..."
read -n 1
blksnap_snapshot_take

#blksnap_stretch_snapshot ${DIFF_STORAGE} 1024

generate_bulk_MB ${MOUNTPOINT_1} "after" 10
check_files ${MOUNTPOINT_1}

echo "Check snapshot before overflow."
echo "press..."
read -n 1

IMAGE_1=${MPDIR}/image0
mkdir -p ${IMAGE_1}
mount /dev/blk-snap-image0 ${IMAGE_1}
check_files ${IMAGE_1}

echo "Try to make snapshot overflow."
echo "press..."
read -n 1
generate_bulk_MB ${MOUNTPOINT_1} "overflow" 10

umount ${IMAGE_1}
blksnap_snapshot_destroy

echo "Check generated data"
check_files ${MOUNTPOINT_1}

echo "Stretch snapshot test finish"
echo "---"

echo 0 > /sys/kernel/livepatch/blk_snap/enabled
sleep 2s
modprobe -r blk-snap
