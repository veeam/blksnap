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

. ./pkg/build_functions.sh

BUILD_DIR=$(pwd)"/build/pkg"
rm -rf ${BUILD_DIR}/
mkdir -p ${BUILD_DIR}/

# copy spec
cp ${CURR_DIR}/blksnap.spec ${BUILD_DIR}/

# prepare content
CONTENT_DIR="${BUILD_DIR}/content"
mkdir -p ${CONTENT_DIR}

# prepare module sources
SRC_DIR=${CONTENT_DIR}/usr/src/blksnap-${VERSION}
mkdir -p ${SRC_DIR}
cp ./module/*.c ${SRC_DIR}/
cp ./module/*.h ${SRC_DIR}/
cp ./module/Makefile* ${SRC_DIR}/
generate_version ${SRC_DIR}/version.h ${VERSION}
cp ./pkg/blksnap.dkms ${SRC_DIR}/dkms.conf

# set package version
find ${BUILD_DIR} -type f -exec sed -i 's/#PACKAGE_VERSION#/'${VERSION}'/g' {} +

cd ${BUILD_DIR}
HOME=${BUILD_DIR} fakeroot rpmbuild -bb --buildroot ${CONTENT_DIR} --nodeps blksnap.spec
mv ./noarch/* ../

cd ${CURR_DIR}
