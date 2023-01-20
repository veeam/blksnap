#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

if [ -f "/usr/bin/blksnap" ] || [ -f "/usr/sbin/blksnap" ]
then
	BLKSNAP=blksnap
else
	BLKSNAP="$(cd ../; pwd)/tools/blksnap/bin/blksnap"
fi

ID=""
BLKSNAP_FILENAME=$(modinfo --field filename blksnap)

blksnap_load()
{
	if [ ${BLKSNAP_FILENAME} = "(builtin)" ]
	then
		return
	fi

	modprobe blksnap $1
	sleep 2s
}

blksnap_unload()
{
	if [ ${BLKSNAP_FILENAME} = "(builtin)" ]
	then
		return
	fi

	echo "Unload module"
	modprobe -r blksnap
}

blksnap_version()
{
	${BLKSNAP} version
}

blksnap_snapshot_create()
{
	PARAM=""

	for DEVICE in $1
	do
		PARAM="${PARAM} --device ${DEVICE}"
	done

	ID=$(${BLKSNAP} snapshot_create ${PARAM})
	echo "New snapshot ${ID} was created"
}

blksnap_snapshot_appendstorage()
{
	local FILE=$1

	echo "Append file ${FILE} to diff storage"
	${BLKSNAP} snapshot_appendstorage --id=${ID} --file=${FILE}
}

blksnap_snapshot_destroy()
{
	echo "Destroy snapshot ${ID}"
	${BLKSNAP} snapshot_destroy --id=${ID}
}

blksnap_snapshot_take()
{
	echo "Take snapshot ${ID}"

	${BLKSNAP} snapshot_take --id=${ID}
}

blksnap_snapshot_collect()
{
	echo "Collect snapshots"

	${BLKSNAP} snapshot_collect
}

blksnap_attach()
{
	local DEVICE=$1

	${BLKSNAP} attach --device=${DEVICE}
}

blksnap_detach()
{
	local DEVICE=$1

	${BLKSNAP} detach --device=${DEVICE}
}

blksnap_cbtinfo()
{
	local DEVICE=$1

	${BLKSNAP} cbtinfo --device=${DEVICE} --file=${CBTMAP}
}

blksnap_readcbt()
{
	local DEVICE=$1
	local CBTMAP=$2

	${BLKSNAP} readcbtmap --device=${DEVICE} --file=${CBTMAP}
}

blksnap_markdirty()
{
	local DIRTYFILE=$2

	${BLKSNAP} markdirtyblock --file=${DIRTYFILE}
}

blksnap_stretch_snapshot()
{
	local DIFF_STORAGE_PATH=$1
	local LIMIT_MB=$2

	${BLKSNAP} stretch_snapshot --id=${ID} --path=${DIFF_STORAGE_PATH} --limit=${LIMIT_MB} &
}

blksnap_get_image()
{
	echo "/dev/blksnap-image_"$(stat -c %t $1)":"$(stat -c %T $1)
}
