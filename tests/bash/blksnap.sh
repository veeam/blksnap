#!/bin/bash -e
BLKSNAP="$(cd ../../; pwd)/bin/blksnap"
ID=""

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

	${BLKSNAP} version
	ID=$(${BLKSNAP} snapshot_create ${PARAM})
	echo "New snapshot ${ID} was created"
}

blksnap_snapshot_append()
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

blksnap_snapshot_take()
{
	echo "Take snapshot ${ID}"

	${BLKSNAP} snapshot_take --id=${ID}
}

blksnap_snapshot_collect()
{
	echo "Collect snapshot ${ID}"

	${BLKSNAP} snapshot_collect --id=${ID}
}

blksnap_tracker_remove()
{
	local DEVICE=$1

	${BLKSNAP} tracker_remove --device=${DEVICE}
}

blksnap_tracker_collect()
{
	${BLKSNAP} tracker_collect
}

blksnap_readcbt()
{
	local DEVICE=$1
	local CBTMAP=$2

	${BLKSNAP} tracker_readcbtmap --device=${DEVICE} --file=${CBTMAP}
}

blksnap_stretch_snapshot()
{
	local DIFF_STORAGE_PATH=$1
	local LIMIT_MB=$2

	${BLKSNAP} stretch_snapshot --id=${ID} --path=${DIFF_STORAGE_PATH} --limit=${LIMIT_MB} &
}
