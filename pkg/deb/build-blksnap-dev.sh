#!/bin/bash -e

if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi

CURR_DIR=$(pwd)
cd "../../"
ROOT_DIR=$(pwd)

BUILD_DIR="build/pkg"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

# build library
mkdir -p "lib/blksnap/bin"
cd "lib/blksnap/bin"
cmake ../
make
cd ${ROOT_DIR}

# copy library
mkdir -p ${BUILD_DIR}/usr/lib/
cp lib/libblksnap.a ${BUILD_DIR}/usr/lib/
# copy include
mkdir -p ${BUILD_DIR}/usr/include/blksnap/
cp include/blksnap/* ${BUILD_DIR}/usr/include/blksnap/

# prepare other package files
cp -r ${CURR_DIR}/blksnap-dev ${BUILD_DIR}/debian

cat > ${BUILD_DIR}/debian/changelog << EOF
blksnap-dev (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <veeam_team@veeam.com>  $(date -R)
EOF

cd ${BUILD_DIR}

# build backage
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}
