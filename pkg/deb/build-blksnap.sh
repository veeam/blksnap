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
BUILD_DIR=${ROOT_DIR}"/build/pkg"

# recreate build directory
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

# copy sources to build directory
cp ${ROOT_DIR}"/CMakeLists.txt" ${BUILD_DIR}/
for SRC_DIR in include lib tests tools
do
	cp -r ${ROOT_DIR}"/"${SRC_DIR} ${BUILD_DIR}/
done

# prepare debian directory
cp -r ${CURR_DIR}/blksnap ${BUILD_DIR}/debian
cat > ${BUILD_DIR}/debian/changelog << EOF
blksnap (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <veeam_team@veeam.com>  $(date -R)
EOF

cd ${BUILD_DIR}

# build backage
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}
