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

SOURCE_DIR="tests"
TARGET_DIR="opt/blksnap/tests"

# build
rm -rf ${SOURCE_DIR}/cpp/bin/*
mkdir -p ${SOURCE_DIR}/cpp/bin/*
cd ${SOURCE_DIR}/cpp/bin
cmake ../
make
cd ${ROOT_DIR}

# copy binaries and scripts
mkdir -p ${BUILD_DIR}/${TARGET_DIR}
for FILE in ${SOURCE_DIR}/*
do
	if [ -f "${FILE}" ]
	then
		cp ${FILE} ${BUILD_DIR}/${TARGET_DIR}/
	fi
done
chmod +x ${BUILD_DIR}/${TARGET_DIR}/*

# prepare other package files
cp -r ${CURR_DIR}/blksnap-tests ${BUILD_DIR}/debian

cat > ${BUILD_DIR}/debian/changelog << EOF
blksnap-tests (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <veeam_team@veeam.com>  $(date -R)
EOF

cd ${BUILD_DIR}

# build backage
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}

