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

