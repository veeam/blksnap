#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Stretch snapshot test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB dikk_storage portion size
blksnap_load "diff_storage_minimum=262144"

# check module is ready
blksnap_version

TESTDIR=~/blksnap-test
MPDIR=/mnt/blksnap-test
DIFF_STORAGE=~/diff_storage/
rm -rf ${TESTDIR}
rm -rf ${MPDIR}
rm -rf ${DIFF_STORAGE}
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

#fallocate --length 256MiB "${DIFF_STORAGE}/diff_storage"
#blksnap_snapshot_appendstorage "${DIFF_STORAGE}/diff_storage"

#echo "Call: ${BLKSNAP} stretch_snapshot --id=${ID} --path=${DIFF_STORAGE} --limit=1024"
blksnap_stretch_snapshot ${DIFF_STORAGE} 1024
#echo "Press for taking snapshot..."
#read -n 1

blksnap_snapshot_take

generate_block_MB ${MOUNTPOINT_1} "after" 100
check_files ${MOUNTPOINT_1}

echo "Check snapshot before overflow."
#echo "press..."
#read -n 1

DEVICE_IMAGE_1=$(blksnap_get_image ${DEVICE_1})
IMAGE_1=${MPDIR}/image0
mkdir -p ${IMAGE_1}
mount ${DEVICE_IMAGE_1} ${IMAGE_1}
check_files ${IMAGE_1}

echo "Try to make snapshot overflow."
#echo "press..."
generate_block_MB ${MOUNTPOINT_1} "overflow" 300

echo "Umount images"
#echo "press..."
umount ${IMAGE_1}

echo "Destroy snapshot"
#echo "press..."
blksnap_snapshot_destroy

#echo "Check generated data"
#check_files ${MOUNTPOINT_1}

echo "Destroy first device"
#echo "press..."
blksnap_detach ${DEVICE_1}
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

blksnap_stretch_wait

blksnap_unload

echo "Stretch snapshot test finish"
echo "---"
