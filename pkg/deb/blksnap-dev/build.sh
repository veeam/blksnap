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
ROOT_DIR=$(pwd)

. pkg/build_functions.sh

BUILD_DIR="build/pkg"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

# build library
mkdir -p "lib/blksnap/bin"
cd "lib/blksnap/bin"
cmake ../
make
cd ${ROOT_DIR}

INSTALL_FILESS=()
# copy library
mkdir -p ${BUILD_DIR}/usr/lib/
cp lib/libblksnap.a ${BUILD_DIR}/usr/lib/
INSTALL_FILES+=("usr/lib/*")
# copy include
mkdir -p ${BUILD_DIR}/usr/include/blksnap/
cp include/blksnap/* ${BUILD_DIR}/usr/include/blksnap/
INSTALL_FILES+=("usr/include/blksnap/*")

# prepare other package files
cp -r ${CURR_DIR}/debian ${BUILD_DIR}/
chmod 0666 ${BUILD_DIR}/debian/control
chmod 0766 ${BUILD_DIR}/debian/rules

cd ${BUILD_DIR}

for FILE in ${INSTALL_FILES[@]}
do
	echo "${FILE} ${FILE}" >> debian/install
done

find ./ -type f -exec sed -i 's/#PACKAGE_VERSION#/'${VERSION}'/g' {} +
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}

