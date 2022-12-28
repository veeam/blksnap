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

# prepare debian directory
rm -rf ${ROOT_DIR}/debian
cp -r ${CURR_DIR}/blksnap ${ROOT_DIR}/debian

cat > ${ROOT_DIR}/debian/changelog << EOF
blksnap (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <veeam_team@veeam.com>  $(date -R)
EOF

cd ${ROOT_DIR}

# build backage
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}
