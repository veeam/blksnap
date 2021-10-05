#!/bin/bash -e

imagefile_make()
{
	local FILEPATH=$1
	local SIZE=$2

	dd if=/dev/zero of=${FILEPATH} bs=${SIZE} count=1
	mkfs.ext4 ${FILEPATH}
}

imagefile_cleanup()
{
	local FILEPATH=$1

	rm -f ${FILEPATH}
}

loop_device_attach()
{
	local FILEPATH=$1
	local DEVICE=""

	DEVICE=$(losetup -f --show ${FILEPATH})
	echo "${DEVICE}"
}

loop_device_detach()
{
	local DEVICE=$1

	losetup -d ${DEVICE}
}

generate_files()
{
	local TARGET_DIR=$1
	local PREFIX=$2
	local COUNT=$3

	for ((ITER = 0 ; ITER < ${COUNT} ; ITER++))
	do
		local FILE=${TARGET_DIR}/${PREFIX}-${ITER}

		dd if=/dev/urandom of=${FILE} count=${RANDOM:1:2} bs=512
		md5sum ${FILE} >> ${TARGET_DIR}/hash.md5
	done
}

check_files()
{
	local TARGET_DIR=$1

	md5sum -c ${TARGET_DIR}/hash.md5
}
