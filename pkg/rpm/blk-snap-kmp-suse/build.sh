#!/bin/bash -e

NAME="blk-snap"
VERSION="1.0.0.0"
VENDOR="Veeam Software Group GmbH"
#VERSION="$1"
#VENDOR="$2"

CURR_DIR=$(pwd)
cd "../../../"

. ./pkg/build_functions.sh

BUILD_DIR="~/rpmbuild"

# prepare module sources
SRC_DIR=${BUILD_DIR}/SOURCES/${NAME}
rm -rf ${SRC_DIR}/
mkdir -p ${SRC_DIR}
cp ./module/*.c ${SRC_DIR}/
cp ./module/*.h ${SRC_DIR}/
cp ./module/Makefile* ${SRC_DIR}/
generate_version ${SRC_DIR}/version.h ${VERSION}
cp ${CURR_DIR}/${NAME}-preamble ${BUILD_DIR}/SPECS/${NAME}-preamble

# prepare spec file
cp ${CURR_DIR}/${NAME}.spec ${BUILD_DIR}/SPECS/${NAME}.spec

chmod +w ${BUILD_DIR}/SPECS/${NAME}.spec
sed -i 's/#PACKAGE_VERSION#/'${VERSION}'/g; s/#PACKAGE_VENDOR#/'${VENDOR}'/g' ${BUILD_DIR}/SPECS/${NAME}.spec
rpmdev-bumpspec --comment="Release ${VERSION}" --userstring=$(whoami) ${BUILD_DIR}/SPECS/${NAME}.spec

#tar -czf ./SOURCES/${NAME}.tar.gz ${NAME}
cd ${BUILD_DIR}
rpmbuild --bb SPECS/${NAME}.spec
cd ${CURR_DIR}
