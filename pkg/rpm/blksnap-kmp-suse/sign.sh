#!/bin/bash -e

PROJECT_DIR=$(pwd)
BUILD_DIR="${HOME}/rpmbuild"

# signing only if private key exist in current directory
if [ not -f ${HOME}/cert/signing_key.pem ]
then
	echo "The directory '${HOME}/cert/' must contain a public 'signing_key.x509' and private 'signing_key.pem' keys."
	exit 1;
fi

cd ${BUILD_DIR}
for PKG in ./RPMS/*/blksnap-kmp*.rpm
do
	modsign-repackage -c ${HOME}/cert/signing_key.x509 -k ${HOME}/cert/signing_key.pem ${PKG}
done
cd ${PROJECT_DIR}
exit 0
