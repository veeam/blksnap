#!/bin/bash -e

make_loop_device()
{
	FILEPATH=$1
	SIZE=$2

	dd if=/dev/zero of=${FILEPATH} bs=${SIZE} count=1
	mkfs.ext4 ${FILEPATH}
}

attach_loop_device()
{
	FILEPATH=$1

	local DEVICE=$(losetup -f --show $FILEPATH)
	echo ${DEVICE}
}

detach_loop_device()
{
	DEVICE=$1

	losetup -d ${DEVICE}
}

