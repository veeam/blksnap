#!/bin/bash -e
NAME=blksnap-tools

if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi
if [ -n "$2" ]
then
	ARCH="$2"
else
	ARCH="amd64"
fi

CURR_DIR=$(pwd)
cd "../../../"
ROOT_DIR=$(pwd)

. pkg/build_functions.sh

BUILD_DIR="build/pkg"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

SOURCE_DIR="tools/blksnap/bin"
TARGET_DIR="usr/bin"

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
INSTALL_FILES+=(${TARGET_DIR}/*)

# prepare other package files
mkdir -p ${BUILD_DIR}/debian
mkdir -p ${BUILD_DIR}/debian/source

cat > ${BUILD_DIR}/debian/source/format << EOF
3.0 (native)
EOF

cat > ${BUILD_DIR}/debian/compat << EOF
9
EOF

cat > ${BUILD_DIR}/debian/control << EOF
Source: ${NAME}
Section: admin
Priority: standard
Maintainer: Veeam Software Group GmbH <veeam_team@veeam.com>
Build-Depends: debhelper (>= 9.0.0),

Package: ${NAME}
Architecture: ${ARCH}
Provides: ${NAME}, ${NAME}-${VERSION}
Depends: ${misc:Depends}
Homepage: https://github.org/veeam/blksnap/
Description: [TBD] The tools for managing the blksnap kernel module.
EOF
chmod 0666 ${BUILD_DIR}/debian/control

cat > ${BUILD_DIR}/debian/copyright << EOF
Format: http://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ${NAME}

Files: debian/*
Copyright: 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
License: GNU LGPL [TBD]
EOF

cat > ${BUILD_DIR}/debian/changelog << EOF
${NAME} (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <https://www.veeam.com/contacts.html>  $(date -R)
EOF

cat > ${BUILD_DIR}/debian/rules << EOF
#!/usr/bin/make -f
# -*- makefile -*-
include /usr/share/dpkg/pkg-info.mk

%:
	dh \$@

EOF
chmod 0766 ${BUILD_DIR}/debian/rules

cd ${BUILD_DIR}

# enumerate installation files
for FILE in ${INSTALL_FILES[@]}
do
	echo "${FILE} "$(dirname ${FILE}) >> debian/install
done

# build backage
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}

