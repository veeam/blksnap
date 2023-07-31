if [ -e "/dev/blksnap" ]
then
  modprobe -r blksnap 2>/dev/null || true
fi
if [ -e "/dev/veeamblksnap" ]
then
  modprobe -r veeamblksnap 2>/dev/null || true
fi

if ! lsmod | grep "bdevfilter" > /dev/null
then
  if [ -e /sys/kernel/livepatch/bdevfilter/enabled ]
  then
    echo 0 > /sys/kernel/livepatch/bdevfilter/enabled || true
    while [ -e /sys/kernel/livepatch/bdevfilter/enabled ]
    do
      sleep 1s
    done
  fi
  modprobe -r bdevfilter || true
fi
