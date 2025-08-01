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

%post -n kmod-%{name}
if [ -d /sys/firmware/efi ]
then
	if [ -f /sys/firmware/efi/efivars/SecureBoot-* ]
	then
		SB_ENABLE=$(od --address-radix=n --format=u1 --skip=4 -N 1 /sys/firmware/efi/efivars/SecureBoot-* | tr -d ' ')
		if [ ${SB_ENABLE} = "1" ]
		then
			echo "Active Secure Boot detected."
			if which mokutil
			then
				if mokutil --list-enrolled | grep -qw "CN=Veeam Software Group GmbH"
				then
					echo "Veeam Software certificate is installed."
				else
					echo "DEPLOY_WARNING: Veeam Software certificate not found."
					echo "DEPLOY_WARNING: Install 'veeam-ueficert' package and complete MOK enrollment."
				fi
			else
				echo "DEPLOY_WARNING: 'mokutil' is not installed."
				echo "DEPLOY_WARNING: Install 'mokutil' package, install 'veeam-ueficert' package and complete MOK enrollment."
			fi
		fi
	fi
fi


%preun -n kmod-%{name}
/usr/sbin/%{name}-loader --unload

%changelog
