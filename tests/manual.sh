#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Manual test start"

DEVICE=$1
if [ -z ${DEVICE} ]
then
	echo "Should be set device ${DEVICE} as first parameter"
	exit 1
fi
echo "Text with experiment device ${DEVICE}"

DIFF_STORAGE=$2
if [ -z ${DIFF_STORAGE} ]
then
	echo "Should be set difference storage ${DIFF_STORAGE} as second parameter"
	exit 1
fi
echo "Difference will be storage on ${DIFF_STORAGE}"

DIFF_STORAGE_SIZE="1GiB"
echo "Difference storage size ${DIFF_STORAGE_SIZE}"

# check module is ready
blksnap_version

echo "Block device prepared, press ..."
read -n 1

blksnap_snapshot_create "${DEVICE}"

DIFF_STORAGE=${DIFF_STORAGE}/diff_storage0
rm -f ${DIFF_STORAGE}/diff_storage0
fallocate --length ${DIFF_STORAGE_SIZE} ${DIFF_STORAGE}
blksnap_snapshot_append ${DIFF_STORAGE}

blksnap_snapshot_take
echo "Snapshot was token"

blksnap_snapshot_collect_all
echo "Make your manual tests, then press ..."
read -n 1

echo "Destroy snapshot"
blksnap_snapshot_destroy

rm ${DIFF_STORAGE}

echo "Manual test finish"
echo "---"
