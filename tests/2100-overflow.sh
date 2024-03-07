#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

if [ -z $1 ]
then
	DIFF_STORAGE_DIR="/dev/shm"
else
	DIFF_STORAGE_DIR=$1
fi
DIFF_STORAGE="${DIFF_STORAGE_DIR}/diff_storage"

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Overflow snapshot test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 128MiB diff_storage portion size
blksnap_load "diff_storage_minimum=262144"

# check module is ready
blksnap_version

TESTDIR=/root/blksnap-test
MPDIR=/mnt/blksnap-test

mkdir -p ${TESTDIR}
rm -rf ${TESTDIR}/*
mkdir -p ${MPDIR}
rm -rf ${MPDIR}*/

MPTESTDIR=$(stat -c %m ${TESTDIR})
DEVICE="/dev/block/"$(mountpoint -d ${MPTESTDIR})

generate_files_direct ${TESTDIR} "before" 5
drop_cache

rm -f ${DIFF_STORAGE}
fallocate --length 128MiB ${DIFF_STORAGE}
blksnap_snapshot_create "${DEVICE}" "${DIFF_STORAGE}" "512M"

generate_files_direct ${TESTDIR} "tracked" 5
drop_cache

blksnap_snapshot_watcher
#echo "Press for taking snapshot..."
#read -n 1

blksnap_snapshot_take

generate_block_MB ${TESTDIR} "after" 100
check_files ${TESTDIR}

echo "Check snapshot before overflow."
#echo "press..."
#read -n 1

DEVICE_IMAGE=$(blksnap_get_image ${DEVICE})
IMAGE=${MPDIR}/image0
mkdir -p ${IMAGE}
mount ${DEVICE_IMAGE} ${IMAGE}
check_files ${IMAGE}/${TESTDIR}

echo "Try to make snapshot overflow."
#echo "press..."
#read -n 1
generate_block_MB ${TESTDIR} "overflow" 768

echo "Umount images"
umount ${IMAGE}

echo "Destroy snapshot"
#echo "press..."
#read -n 1

blksnap_snapshot_destroy

#echo "Check generated data"
#check_files ${TESTDIR}

blksnap_watcher_wait

blksnap_detach ${DEVICE}

blksnap_unload

echo "Cleanup test directory" ${TESTDIR}
rm -rf ${TESTDIR}/*

echo "Overflow snapshot test finish"
echo "---"
