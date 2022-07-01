#!/bin/bash -e

FILE=$(basename $1)
GUARD=${FILE/"."/"_"}

if [ -z $2 ]
then
        GUARD="__LINUX_"${GUARD^^}
else
        GUARD="__$2_"${GUARD^^}
fi

sed -i -e "s/#pragma once/#ifndef ${GUARD}\n#define ${GUARD}\n/g" $1
echo "#endif /* ${GUARD} */" >> $1
