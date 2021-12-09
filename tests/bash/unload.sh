#!/bin/bash -e

modprobe -r blksnap

echo 0 > /sys/kernel/livepatch/bdevfilter/enabled
sleep 2s
modprobe -r bdevfilter
