#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Diff storage test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB diff_storage portion size
blksnap_load "diff_storage_minimum=262144"

# check module is ready
blksnap_version

if [ -z $1 ]
then
	TEST_DIR=${HOME}/blksnap-test
else
	TEST_DIR=$(realpath $1"/blksnap-test")
fi
mkdir -p ${TEST_DIR}
rm -rf ${TEST_DIR}/*

MP_TEST_DIR=$(stat -c %m ${TEST_DIR})
DEVICE="/dev/block/"$(mountpoint -d ${MP_TEST_DIR})
echo "Test directory [${TEST_DIR}] on device [${DEVICE}] selected"

RELATIVE_TEST_DIR=${TEST_DIR#${MP_TEST_DIR}}

MP_DIR=/mnt/blksnap-test

rm -rf ${MP_DIR}
mkdir -p ${MP_DIR}

generate_block_MB ${TEST_DIR} "before" 10
check_files ${TEST_DIR}

DIFF_STORAGE=${TEST_DIR}/diff_storage
fallocate --length 1GiB ${DIFF_STORAGE}

blksnap_snapshot_create "${DEVICE}" "${DIFF_STORAGE}" "1G"
blksnap_snapshot_watcher
blksnap_snapshot_take

generate_block_MB ${TEST_DIR} "after" 1000
check_files ${TEST_DIR}

IMAGE=${MP_DIR}/image0
mkdir -p ${IMAGE}

echo "Mount image"
DEVICE_IMAGE=$(blksnap_get_image ${DEVICE})
mount ${DEVICE_IMAGE} ${IMAGE}
# for XFS filesystem nouuid option needed
#mount -o nouuid /dev/blksnap-image0 ${IMAGE}

check_files ${IMAGE}/${RELATIVE_TEST_DIR}

echo "Umount image"
umount ${IMAGE}

echo "Destroy snapshot"
blksnap_snapshot_destroy

blksnap_detach ${DEVICE}

blksnap_watcher_wait

blksnap_unload

echo "Diff storage test finish"
echo "---"
