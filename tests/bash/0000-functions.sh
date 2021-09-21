#!/bin/bash -e

imagefile_make()
{
	FILEPATH=$1
	SIZE=$2

	dd if=/dev/zero of=${FILEPATH} bs=${SIZE} count=1
	mkfs.ext4 ${FILEPATH}
}

imagefile_cleanup()
{
	FILEPATH=$1

	rm -f ${FILEPATH}
}

loop_device_attach()
{
	FILEPATH=$1

	local DEVICE=$(losetup -f --show $FILEPATH)
	echo ${DEVICE}
}

loop_device_detach()
{
	DEVICE=$1

	losetup -d ${DEVICE}
}

