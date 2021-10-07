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
	local GEN_FILE_PWD=$(pwd)

	echo "generate files in ${TARGET_DIR}"
	cd ${TARGET_DIR}

	for ((ITER = 0 ; ITER < ${COUNT} ; ITER++))
	do
		local FILE=./${PREFIX}-${ITER}
		local SZ=${RANDOM:1:2}
		dd if=/dev/urandom of=${FILE} count=$((SZ + 8)) bs=512
		md5sum ${FILE} >> ${TARGET_DIR}/hash.md5
	done
	cd ${GEN_FILE_PWD}
}

check_files()
{
	local TARGET_DIR=$1
	local CHECH_FILE_PWD=$(pwd)

	echo "Check files in ${TARGET_DIR}"
	cd ${TARGET_DIR}

	md5sum -c "./hash.md5"

	cd ${CHECH_FILE_PWD}
}

drop_cache()
{
	echo 3 > /proc/sys/vm/drop_caches
}
