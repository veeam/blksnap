#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

. ../functions.sh
. ../blksnap.sh

echo "---"
echo "FIO COW algorithm performance test"

fio --version
blksnap_load
blksnap_version

if [ -z $1 ]
then
	echo "You must specify the path to the block device for testing."
	exit -1
else
	DEVICE="$1"
fi

fio --filename "${DEVICE}" --section sequental_read ./blksnap.fio
fio --filename "${DEVICE}" --section random_read_4k ./blksnap.fio

blksnap_snapshot_create "${DEVICE}" "/dev/shm" "1G"
blksnap_snapshot_watcher
blksnap_snapshot_take

DEVICE_IMAGE=$(blksnap_get_image ${DEVICE})

fio --filename "${DEVICE}" --section random_write_4k ./blksnap.fio
fio --filename "${DEVICE}" --section sequental_read ./blksnap.fio

echo "Destroy snapshot"
blksnap_snapshot_destroy
blksnap_detach "${DEVICE}"
blksnap_watcher_wait

blksnap_unload

echo "FIO COW algorithm performance test finish"
echo "---"
