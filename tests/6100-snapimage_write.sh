#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0+

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Snapshot direct write test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB.
modprobe blksnap diff_storage_minimum=262144

modprobe blksnap
sleep 2s

# check module is ready
blksnap_version


echo "Should use loop device"

TESTDIR="~/blksnap-test"
rm -rf ${TESTDIR}
mkdir -p ${TESTDIR}

#echo "Create original loop device"
LOOPFILE="${TESTDIR}/blksnap-original.img"
dd if=/dev/zero of=${LOOPFILE} count=256 bs=1M

DEVICE=$(loop_device_attach ${LOOPFILE})

if [ -z $1 ]
then
	END="1"
else
	END="$1"
fi

DIFF_STORAGE="${TESTDIR}/diff_storage"
mkdir -p ${DIFF_STORAGE}

fallocate --length 256MiB "${DIFF_STORAGE}/#0" &

for ITERATOR in $(seq 1 $END)
do
	echo "Itearation: ${ITERATOR}"

	blksnap_snapshot_create ${DEVICE}
	blksnap_snapshot_appendstorage "${DIFF_STORAGE}/#0"
	blksnap_snapshot_take

	DEVICE_IMAGE=$(blksnap_get_image ${DEVICE})

	FILE="${TESTDIR}/image-it#${ITERATOR}"
	generate_file_magic ${FILE} 2048

	dd if=${FILE} of=${DEVICE} bs=1KiB count=1024 seek=0 oflag=direct

	dd if=${FILE} of=${DEVICE_IMAGE} bs=1KiB count=1024 seek=126 oflag=direct conv=sync

	dd if=${FILE} of=${DEVICE_IMAGE} bs=1KiB count=1024 seek=$((2048 + 126)) oflag=direct conv=sync

	dd if=${FILE} of=${DEVICE_IMAGE} bs=1KiB count=1024 seek=$((4096 + 126)) oflag=direct conv=sync


	sync ${DEVICE_IMAGE}
	echo "pause, press ..."
	read -n 1

	dd if=${DEVICE_IMAGE} of="${FILE}_copy" bs=1KiB count=1024 skip=126 iflag=direct conv=sync
	cmp ${FILE} "${FILE}_copy" && echo "Files are equal."

	dd if=${DEVICE_IMAGE} of="${FILE}_copy1" bs=1KiB count=1024 skip=$((2048 + 126)) iflag=direct conv=sync
	cmp ${FILE} "${FILE}_copy1" && echo "Files are equal."

	dd if=${DEVICE_IMAGE} of="${FILE}_copy2" bs=1KiB count=1024 skip=$((4096 + 126)) oflag=direct conv=sync
	cmp ${FILE} "${FILE}_copy2" && echo "Files are equal."


	echo "pause, press ..."
	read -n 1

	blksnap_snapshot_destroy
done

echo "Destroy original loop device"

blksnap_detach ${DEVICE}

loop_device_detach ${DEVICE}
imagefile_cleanup ${LOOPFILE}

echo "Cleanup test directory [${TESTDIR}]"
rm -rf "${TESTDIR}"

echo "Unload module"
modprobe -r blksnap

echo "Snapshot direct write test finish"
echo "---"
