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
echo "Snapshot direct write test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB.
modprobe blksnap diff_storage_minimum=262144

modprobe blksnap
sleep 2s

# check module is ready
blksnap_version


echo "Should use loop device"

TESTDIR="${HOME}/blksnap-test"
rm -rf ${TESTDIR}
mkdir -p ${TESTDIR}

#echo "Create original loop device"
LOOPFILE="${TESTDIR}/blksnap-original.img"
dd if=/dev/zero of=${LOOPFILE} count=256 bs=1M

DEVICE=$(loop_device_attach ${LOOPFILE} ${BLOCK_SIZE})

if [ -z $1 ]
then
	ITERATION_CNT="1"
else
	ITERATION_CNT="$1"
fi

DIFF_STORAGE="${DIFF_STORAGE_DIR}/diff_storage"
fallocate --length 1GiB ${DIFF_STORAGE}

for ITERATOR in $(seq 1 $ITERATION_CNT)
do
	echo "Itearation: ${ITERATOR}"

	blksnap_snapshot_create ${DEVICE} "${DIFF_STORAGE}" "256M"
	blksnap_snapshot_take

	DEVICE_IMAGE=$(blksnap_get_image ${DEVICE})

	FILE="${TESTDIR}/image-it#${ITERATOR}"
	generate_file_magic ${FILE} 2048

	dd if=${FILE} of=${DEVICE} bs=1KiB count=1024 seek=0 oflag=direct status=none

	dd if=${FILE} of=${DEVICE_IMAGE} bs=1KiB count=1024 seek=126 oflag=direct status=none

	dd if=${FILE} of=${DEVICE_IMAGE} bs=1KiB count=1024 seek=$((2048 + 126)) oflag=direct status=none

	dd if=${FILE} of=${DEVICE_IMAGE} bs=1KiB count=1024 seek=$((4096 + 126)) oflag=direct status=none


	sync ${DEVICE_IMAGE}
	# echo "pause, press ..."
	# read -n 1

	dd if=${DEVICE_IMAGE} of="${FILE}_copy" bs=1KiB count=1024 skip=126 iflag=direct status=none
	cmp ${FILE} "${FILE}_copy" && echo "Files are equal."

	dd if=${DEVICE_IMAGE} of="${FILE}_copy1" bs=1KiB count=1024 skip=$((2048 + 126)) iflag=direct status=none
	cmp ${FILE} "${FILE}_copy1" && echo "Files are equal."

	dd if=${DEVICE_IMAGE} of="${FILE}_copy2" bs=1KiB count=1024 skip=$((4096 + 126)) oflag=direct status=none
	cmp ${FILE} "${FILE}_copy2" && echo "Files are equal."


	# echo "pause, press ..."
	# read -n 1

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
