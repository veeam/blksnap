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
STRETCH_PROCESS_PID=""

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
	modprobe -r blksnap 2>&1 || sleep 1 && modprobe -r blksnap && echo "Unload success"
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
	PARAM="${PARAM} --file $2"
	PARAM="${PARAM} --limit $3"

	ID=$(${BLKSNAP} snapshot_create ${PARAM})
	echo "New snapshot ${ID} was created"
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

blksnap_snapshot_watcher()
{
	${BLKSNAP} snapshot_watcher --id=${ID} &
	STRETCH_PROCESS_PID=$!
}
blksnap_watcher_wait()
{
	echo "Waiting for streach process terminate"
	wait ${STRETCH_PROCESS_PID}
}

blksnap_get_image()
{
	${BLKSNAP} snapshot_info --field image --device $1
}

blksnap_cleanup()
{
	for ID in $(${BLKSNAP} snapshot_collect)
	do
		${BLKSNAP} snapshot_destroy --id=${ID}
	done
}
