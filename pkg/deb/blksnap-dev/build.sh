#!/bin/bash -e
NAME=blksnap-dev

if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi

if [ -n "$(dpkg-architecture -q DEB_HOST_ARCH)" ]
then
	ARCH="$(dpkg-architecture -q DEB_HOST_ARCH)"
else
	ARCH="amd64"
fi

CURR_DIR=$(pwd)
cd "../../../"
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
INSTALL_FILES+=("usr/lib/*")
# copy include
mkdir -p ${BUILD_DIR}/usr/include/blksnap/
cp include/blksnap/* ${BUILD_DIR}/usr/include/blksnap/
INSTALL_FILES+=("usr/include/blksnap/*")

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
Section: devel
Priority: standard
Maintainer: Veeam Software Group GmbH <veeam_team@veeam.com>
Build-Depends: debhelper (>= 9.0.0), bash, g++, cmake, uuid-dev, libboost-filesystem-dev

Package: ${NAME}
Architecture: ${ARCH}
Provides: ${NAME}, ${NAME}-${VERSION}
Depends: \${misc:Depends}
Homepage: https://github.org/veeam/blksnap/
Description: [TBD] The static library and header files for managing the
        blksnap kernel module.
EOF
chmod 0666 ${BUILD_DIR}/debian/control

cat > ${BUILD_DIR}/debian/copyright << EOF
Format: http://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ${NAME}

Files: debian/*
Copyright: 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
License: GNU LGPL-3.0
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
