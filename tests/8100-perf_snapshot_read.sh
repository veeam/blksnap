#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

#
# Requirement:
#	The 'TESTDIR' directory and the 'DIFF_STORAGE_DIR' directory
#	must be on different block devices.
#

if [ -z $1 ]
then
	TESTDIR="/home/user"
else
	TESTDIR=$2
fi
DEVICE_1=$(findmnt -n -o SOURCE -T ${TESTDIR})
echo "Test directory ${TESTDIR} on device ${DEVICE_1}"

if [ -z $2 ]
then
	DIFF_STORAGE_DIR="/tmp"
else
	DIFF_STORAGE_DIR=$1
fi

SOURCE_FILE=${DIFF_STORAGE_DIR}/test_file.dat
if [ ! -f ${SOURCE_FILE} ]
then
	dd if=/dev/urandom of=${SOURCE_FILE} count=$((7 * 1024)) bs=1M
fi
TARGET_FILE=${TESTDIR}/test_file.dat

echo "Diff storage directory ${DIFF_STORAGE_DIR}"

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Perf snapshot read test start"

blksnap_load

# check module is ready
blksnap_version

DIFF_STORAGE="${DIFF_STORAGE_DIR}/diff_storage"
fallocate --length 1024MiB ${DIFF_STORAGE}

blksnap_snapshot_create "${DEVICE_1}" "${DIFF_STORAGE}" "8G"
blksnap_snapshot_take

#echo "Write something" > ${MOUNTPOINT_1}/something.txt
echo "Write to original"
dd if=${SOURCE_FILE} of=${TARGET_FILE} bs=1M oflag=direct

echo "Read from snapshot "
perf record -ag dd if=${TARGET_FILE} of=/dev/null bs=1M iflag=direct

#echo "Destroy snapshot, press ..."
#read -n 1
blksnap_snapshot_destroy

echo "Destroy device"
blksnap_detach ${DEVICE_1}

rm ${TARGET_FILE}
rm ${DIFF_STORAGE}

blksnap_unload

echo "Perf snapshot read test finish"
echo "---"

perf report
