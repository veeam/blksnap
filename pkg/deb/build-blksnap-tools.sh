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

SOURCE_DIR="tools/blksnap/bin"
TARGET_DIR="usr/sbin"

# build
rm -rf ${SOURCE_DIR}/*
mkdir -p ${SOURCE_DIR}
cd ${SOURCE_DIR}
cmake ../
make
cd ${ROOT_DIR}

# copy binaries
mkdir -p ${BUILD_DIR}/${TARGET_DIR}
cp ${SOURCE_DIR}/blksnap ${BUILD_DIR}/${TARGET_DIR}/
chmod +x ${BUILD_DIR}/${TARGET_DIR}/*

# prepare other package files
cp -r ${CURR_DIR}/blksnap-tools ${BUILD_DIR}/debian

cat > ${BUILD_DIR}/debian/changelog << EOF
blksnap-tools (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <veeam_team@veeam.com>  $(date -R)
EOF

cd ${BUILD_DIR}

# build backage
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}

