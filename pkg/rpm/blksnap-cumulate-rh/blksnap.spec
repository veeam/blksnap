Name:    blksnap
Version: #PACKAGE_VERSION#
Release: 1%{?dist}
Packager: "#PACKAGE_VENDOR#"
Vendor:  "#PACKAGE_VENDOR#"
Group:   System Environment/Kernel
License: GPL-2
Summary: Veeam Agent for Linux (kernel module)
URL:     http://github.com/veeam/blksnap
ExclusiveOS:    linux
ExclusiveArch:  %{ix86} x86_64

Source0: %{name}-%{version}.tar.gz
Source1: %{name}-loader

BuildRoot: %{_tmppath}/%{name}-buildroot

%description
This kernel module implements snapshot and changed block tracking
functionality used by Veeam Agent for Linux - simple and FREE backup agent
designed to ensure the Availability of your Linux server instances, whether
they reside in the public cloud or on premises.

%package -n kmod-%{name}
Summary: Veeam Agent for Linux (kernel module)
Group: System Environment/Kernel
Provides: %{name} = %{version}
Requires: kmod python3
Conflicts: veeamsnap

%description -n kmod-%{name}
This kernel module implements snapshot and changed block tracking
functionality used by Veeam Agent for Linux - simple and FREE backup agent
designed to ensure the Availability of your Linux server instances, whether
they reside in the public cloud or on premises.

# Disable the building of the debug package(s).
%define debug_package %{nil}

%prep
%setup -q -n %{name}-%{version}

%build
for kver in %{kversion}; do
	KSRC=%{_usrsrc}/kernels/${kver}

	mkdir -p $PWD/${kver}
	cp -rf $PWD/module/ $PWD/${kver}/
	%{__make} -j$(nproc) -C "${KSRC}" %{?_smp_mflags} modules M=$PWD/${kver}/module
done

for kver in %{kversion}; do
	KSRC=%{_usrsrc}/kernels/${kver}

	export INSTALL_MOD_PATH=%{buildroot}
	export INSTALL_MOD_DIR=extra
	export INSTALL_MOD_STRIP=--strip-debug

	%{__make} -C "${KSRC}" modules_install M=$PWD/${kver}/module

	%{__rm} -f %{buildroot}/lib/modules/${kver}/modules.*
done
%{__install} -d %{buildroot}/usr/sbin/
%{__install} -m 744 %{SOURCE1} %{buildroot}/usr/sbin/

%files -n kmod-%{name}
%defattr(644,root,root,755)
/lib/modules/
/usr/sbin/%{name}-loader
%attr(744,root,root) /usr/sbin/%{name}-loader

%clean
%{__rm} -rf %{buildroot}

%posttrans -n kmod-%{name}
#!/bin/bash -e

function check_certificate()
{
	local MODULE_NAME="veeamblksnap"
	local NAME=%{name}

	local RET=0
	/usr/sbin/${NAME}-loader --check 1>/dev/null || RET=$?
	if [ ${RET} -gt 1 ]
	then
		echo "DEPLOY_WARNING: Failed to execute 'modinfo' for ${MODULE_NAME} module."
		echo "DEPLOY_WARNING: Check is '${NAME}' package is installed."
		return 1
	fi

	local MODULE_AUTHOR=$(/usr/sbin/${NAME}-loader --modinfo  2>/dev/null | grep "^author:" | sed 's/author:[[:space:]]*//')
	if [ $? -ne 0 ] || [ -z "${MODULE_AUTHOR}" ]
	then
		echo "DEPLOY_WARNING: Cannot get 'author' value for ${MODULE_NAME} module."
		echo "DEPLOY_WARNING: Check is '${NAME}' package is installed."
		return 1
	fi
	echo "Found module by ${MODULE_AUTHOR}."

	local MODULE_SIG_KEY=$(/usr/sbin/${NAME}-loader --modinfo 2>/dev/null | grep "^sig_key:" | sed 's/sig_key:[[:space:]]*//')
	if [ $? -ne 0 ] || [ -z "${MODULE_SIG_KEY}" ]
	then
		echo "DEPLOY_WARNING: Cannot get 'sig_key' value for ${MODULE_NAME} module."
		echo "DEPLOY_WARNING: Check is '${NAME}' package is installed."
		return 1
	fi
	echo "Found Veeam Software module with signature key: ${MODULE_SIG_KEY}."

	local SIG_KEY_CLEAN=$(echo "${MODULE_SIG_KEY}" | tr -d ': ' | tr '[:lower:]' '[:upper:]')

	local CERT_SN=$(mokutil --list-enrolled 2>/dev/null | awk -v cn="CN=${MODULE_AUTHOR}" -v key="${SIG_KEY_CLEAN}" '
		/Serial Number:/ {
			getline
			gsub(/^[[:space:]]+/, "")
			gsub(/[[:space:]]+$/, "")
			serial = $0
			clean = toupper(serial)
			gsub(/[: ]/, "", clean)
		}
		/Subject:/ {
			if ($0 ~ cn && clean == key) {
				print serial
				exit
			}
			serial = ""
			clean = ""
		}
	')
	if [ -z "${CERT_SN}" ]
	then
		echo "DEPLOY_WARNING: Veeam Software certificate matching the module signature was not found."
		echo "DEPLOY_WARNING: Module signature key: ${MODULE_SIG_KEY}."
		echo "DEPLOY_WARNING: Install 'veeam-ueficert' package and complete MOK enrollment."
		return 1
	fi
	echo "Found Veeam Software certificate with serial number: ${CERT_SN}."

	return 0
}

SB_ENABLE=""
if [ -d /sys/firmware/efi ]
then
	if [ -f /sys/firmware/efi/efivars/SecureBoot-* ]
	then
		SB_ENABLE=$(od --address-radix=n --format=u1 --skip=4 -N 1 /sys/firmware/efi/efivars/SecureBoot-* | tr -d ' ')
		if [ ${SB_ENABLE} = "1" ]
		then
			echo "Active Secure Boot detected."
			if which mokutil 1>/dev/null
			then
				if check_certificate
				then
					echo "Veeam Software certificate is installed."
				fi
			else
				echo "DEPLOY_WARNING: 'mokutil' is not installed."
				echo "DEPLOY_WARNING: Install 'mokutil' package, install 'veeam-ueficert' package and complete MOK enrollment."
			fi
		else
			echo "Secure Boot is not active."
		fi
	fi
fi
if [ -z ${SB_ENABLE} ]
then
	echo "Secure Boot was not detected."
fi

%preun -n kmod-%{name}
/usr/sbin/%{name}-loader --unload

%changelog
