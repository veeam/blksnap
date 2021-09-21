#!/bin/bash -e

TESTDIR=/tmp/blk-snap-test
mkdir -p ${TESTDIR}
make_loop_device ${TESTDIR}/simple.img 64M

DEVICE=$(attach_loop_device ${TESTDIR}/simple.img)

echo "new device ${DEVICE}"

detach_loop_device ${DEVICE}

blksnap_snapshot_create_inmem ${DEVICE}



blksnap_snapshot_destroy
