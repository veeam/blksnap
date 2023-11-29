#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

if [ -z $1 ]
then
	DIFF_STORAGE_DIR=${HOME}
else
	DIFF_STORAGE_DIR=$1
fi
echo "Diff storage directory ${DIFF_STORAGE_DIR}"

. ./functions.sh
. ./blksnap.sh
BLOCK_SIZE=$(block_size_mnt ${DIFF_STORAGE_DIR})

echo "---"
echo "Inline encryption test start"
echo "devices block size ${BLOCK_SIZE}"

blksnap_load
blksnap_version

TESTDIR=${HOME}/blksnap-test
rm -rf ${TESTDIR}
mkdir -p ${TESTDIR}

MPDIR=/mnt/blksnap-test
rm -rf ${MPDIR}
mkdir -p ${MPDIR}

# Create device
IMAGEFILE_1=${TESTDIR}/simple_1.img
imagefile_make ${IMAGEFILE_1} 64

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1} ${BLOCK_SIZE})
mkfs.ext4 -O encrypt ${DEVICE_1}
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount -o inlinecrypt,test_dummy_encryption ${DEVICE_1} ${MOUNTPOINT_1}
#tune2fs -O encrypt ${DEVICE_1}

generate_files_direct ${MOUNTPOINT_1} "before" 3
drop_cache

blksnap_snapshot_create "${DEVICE_1}" "${DIFF_STORAGE_DIR}" "64M"
blksnap_snapshot_take

echo "Write to original"
generate_files_direct ${MOUNTPOINT_1} "after" 3
umount ${MOUNTPOINT_1}

echo "Try to read snapshot image"
DEVICE_IMAGE_1=$(blksnap_get_image ${DEVICE_1})
set +e
dd if=${DEVICE_IMAGE_1} of=${TESTDIR}/image1 bs=1KiB oflag=direct status=none
drop_cache
set -e

blksnap_snapshot_destroy

echo "Destroy device"
blksnap_detach ${DEVICE_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

blksnap_unload

echo "Inline encryption test finish"
echo "---"
