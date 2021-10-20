#!/bin/bash -e

VERSION="$1"
if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi

CURR_DIR=$(pwd)
cd "../../../"

. pkg/build_functions.sh

BUILD_DIR="build/pkg"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

# prepare module sources
mkdir -p ${BUILD_DIR}/src
cp module/*.c ${BUILD_DIR}/src/
cp module/*.h ${BUILD_DIR}/src/
cp module/Makefile* ${BUILD_DIR}/src/
generate_version ${BUILD_DIR}/src/version.h ${VERSION}

# prepare other package files
cp -r pkg/deb/blk-snap-dkms/debian ${BUILD_DIR}
cp pkg/blk-snap.dkms ${BUILD_DIR}/debian/
chmod 0666 ${BUILD_DIR}/debian/control
chmod 0766 ${BUILD_DIR}/debian/rules
find ${BUILD_DIR} -type f -exec sed -i 's/#PACKAGE_VERSION#/'${VERSION}'/g' {} +

cd ${BUILD_DIR} && dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}
