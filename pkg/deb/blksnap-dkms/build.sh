#!/bin/bash -e
NAME=blksnap

VERSION="$1"
if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi
ARCH="all"

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
mkdir -p ${BUILD_DIR}/debian
mkdir -p ${BUILD_DIR}/debian/source

cp ./pkg/${NAME}.dkms ${BUILD_DIR}/debian/
sed -i 's/#PACKAGE_VERSION#/'${VERSION}'/g' ${BUILD_DIR}/debian/${NAME}.dkms

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
Build-Depends: debhelper (>= 9.0.0), dh-dkms | dkms

Package: ${NAME}
Architecture: ${ARCH}
Provides: ${NAME}, ${NAME}-${VERSION}
Depends: dkms, \${shlibs:Depends}, \${misc:Depends}
Conflicts: veeamsnap
Replaces: veeamsnap
Homepage: https://github.org/veeam/blksnap/
Description: Veeam Agent for Linux (kernel module)
 This kernel module implements snapshot and changed block tracking
 functionality used by Veeam Agent for Linux - simple and FREE backup agent
 designed to ensure the Availability of your Linux server instances, whether
 they reside in the public cloud or on premises.
EOF
chmod 0666 ${BUILD_DIR}/debian/control

cat > ${BUILD_DIR}/debian/copyright << EOF
Format: http://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ${NAME}

Files: debian/*
Copyright: 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
License: GNU GPL-2.0
EOF

cat > ${BUILD_DIR}/debian/changelog << EOF
${NAME} (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <https://www.veeam.com/contacts.html>  $(date -R)
EOF

cat > ${BUILD_DIR}/debian/postrm << EOF
#!/bin/sh
set -e

checkModule()
{
	if ! lsmod | grep "\$1" > /dev/null
	then
		return 1
	fi
	return 0
}

case "\$1" in
	remove|upgrade|failed-upgrade|abort-install|abort-upgrade|disappear|purge)
		if checkModule "blksnap"
		then
			rmmod blksnap || true
		fi
		if checkModule "bdevfilter"
		then
			echo 0 > /sys/kernel/livepatch/bdevfilter/enabled || true
			sleep 3s
			rmmod bdevfilter || true
		fi
	;;
	*)
		echo "prerm called with unknown argument '\$1'" >&2
		exit 1
	;;
esac

exit 0
EOF
chmod 0766 ${BUILD_DIR}/debian/postrm

cat > ${BUILD_DIR}/debian/rules << EOF
#!/usr/bin/make -f
# -*- makefile -*-
include /usr/share/dpkg/pkg-info.mk

%:
	dh \$@ --with dkms

override_dh_install:
	dh_install src/* usr/src/${NAME}-${VERSION}/

override_dh_dkms:
	dh_dkms -V ${VERSION}

EOF
chmod 0766 ${BUILD_DIR}/debian/rules

cd ${BUILD_DIR} && dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}
