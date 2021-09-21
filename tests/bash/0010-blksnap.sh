#!/bin/bash -e
BLKSNAP="$(cd ../../; pwd)/bin/blksnap"
ID=""

blksnap_snapshot_create_inmem()
{
	DEVICE=$1

	${BLKSNAP} version
	ID=$(${BLKSNAP} snapshot_create --device ${DEVICE})
	echo "New snapshot ${ID} was created"
}

blksnap_snapshot_destroy()
{
	echo "Destroy snapshot ${ID}"
	${BLKSNAP} snapshot_destroy --id=${ID}
}
