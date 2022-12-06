Name:    blksnap
Version: #PACKAGE_VERSION#
Release: 1%{?dist}
Packager: "#PACKAGE_VENDOR#"
Vendor:  "#PACKAGE_VENDOR#"
Group:   System Environment/Kernel
License: GPL-2
Summary: Block device snapshot (kernel module)
URL:     http://github.com/veeam/blksnap
ExclusiveOS:    linux
ExclusiveArch:  %{ix86} x86_64

Source0: %{name}-%{version}.tar.gz
Source1: %{name}-loader

BuildRoot: %{_tmppath}/%{name}-buildroot

%description
This package provides the blksnap kernel module.
It is built to depend upon the specific ABI provided by a range of releases
of the same variant of the Linux kernel and not on any one specific build.

%package -n kmod-%{name}
Summary: %{name} kernel module
Group: System Environment/Kernel
Provides: %{name} = %{version}
Requires: python3
Conflicts: veeamsnap

%description -n kmod-%{name}
This package provides the ${kmod_name} kernel modules built for the Linux
kernels %{kversion} for the %{_target_cpu} family of processors.

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

%preun -n kmod-%{name}
/usr/sbin/%{name}-loader --unload

%changelog
