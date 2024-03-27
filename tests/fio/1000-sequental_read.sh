#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

. ../functions.sh
. ../blksnap.sh

echo "---"
echo "FIO sequental read test"

fio --version
blksnap_load
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

MP_DIR=/mnt/blksnap-test
rm -rf ${MP_DIR}
mkdir -p ${MP_DIR}

DIR=${MP_TEST_DIR} fio --section sequental_read ./blksnap.fio

blksnap_snapshot_create "${DEVICE}" "/dev/shm" "1G"
blksnap_snapshot_watcher
blksnap_snapshot_take


IMAGE=${MP_DIR}/image0
mkdir -p ${IMAGE}

echo "Mount image"
DEVICE_IMAGE=$(blksnap_get_image ${DEVICE})
mount ${DEVICE_IMAGE} ${IMAGE}

DIR="${IMAGE}/${MP_TEST_DIR}" fio --section sequental_read ./blksnap.fio

echo "Umount image"
umount ${IMAGE}

echo "Destroy snapshot"
blksnap_snapshot_destroy
blksnap_detach ${DEVICE}
blksnap_watcher_wait

blksnap_unload

echo "FIO sequental read test finish"
echo "---"
