checkModule()
{
  if ! lsmod | grep "$1" > /dev/null
  then
    return 1
  fi
  return 0
}

if checkModule blksnap
then
  modprobe -r blksnap || true
fi

if checkModule bdevfilter
then
  modprobe -r bdevfilter || true
fi
