#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

if [ -z $1 ]
then
	DIFF_STORAGE_DIR=${HOME}
else
	DIFF_STORAGE_DIR=$1
fi

. ./functions.sh
. ./blksnap.sh
BLOCK_SIZE=$(block_size_mnt ${DIFF_STORAGE_DIR})

echo "---"
echo "Stretch snapshot test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 128MiB diff_storage portion size
blksnap_load "diff_storage_minimum=262144"

# check module is ready
blksnap_version

TESTDIR=${HOME}/blksnap-test
MPDIR=/mnt/blksnap-test
DIFF_STORAGE="${DIFF_STORAGE_DIR}/diff_storage"

rm -rf ${TESTDIR}
rm -rf ${MPDIR}
mkdir -p ${TESTDIR}
mkdir -p ${MPDIR}

# create first device
IMAGEFILE_1=${TESTDIR}/simple_1.img
imagefile_make ${IMAGEFILE_1} 4096

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1} ${BLOCK_SIZE})
mkfs.ext4 ${DEVICE_1}
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

generate_files_direct ${MOUNTPOINT_1} "before" 5
drop_cache

rm -f ${DIFF_STORAGE}
fallocate --length 128MiB ${DIFF_STORAGE}
blksnap_snapshot_create "${DEVICE_1}" "${DIFF_STORAGE}" "512M"

generate_files_direct ${MOUNTPOINT_1} "tracked" 5
drop_cache

blksnap_snapshot_watcher
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
generate_block_MB ${MOUNTPOINT_1} "overflow" 768

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

blksnap_watcher_wait

blksnap_unload

echo "Stretch snapshot test finish"
echo "---"
