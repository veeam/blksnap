#!/bin/bash -e

cat ./v3-* | grep --color='auto' -P -n '[^\x00-\x7F]'

git send-email --smtp-ssl-cert-path ~/Downloads/prgmbx02.pem	\
	--to=axboe@kernel.dk	\
	--to=hch@infradead.org	\
	--to=corbet@lwn.net	\
	--to=snitzer@kernel.org \
	--cc=viro@zeniv.linux.org.uk  \
	--cc=brauner@kernel.org  \
	--cc=willy@infradead.org \
	--cc=kch@nvidia.com \
	--cc=martin.petersen@oracle.com \
	--cc=vkoul@kernel.org \
	--cc=ming.lei@redhat.com \
	--cc=gregkh@linuxfoundation.org \
	--cc=linux-block@vger.kernel.org \
	--cc=linux-doc@vger.kernel.org \
	--cc=linux-kernel@vger.kernel.org \
	--cc=linux-fsdevel@vger.kernel.org \
	--cc=sergei.shtepa@veeam.com \
./v3-0000* ./v3-0001* ./v3-0002* ./v3-0003* ./v3-0004* ./v3-0005* ./v3-0006* ./v3-0007* ./v3-0008* ./v3-0009* ./v3-0010* ./v3-0011*
