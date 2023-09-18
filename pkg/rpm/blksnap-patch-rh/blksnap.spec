Name:    blksnap
Version: #PACKAGE_VERSION#
Release: %{?release}%{?dist}
Packager: "#PACKAGE_VENDOR#"
Vendor:  "#PACKAGE_VENDOR#"
Group:   System Environment/Kernel
License: GPL-2
Summary: Block device snapshot (kernel module)
URL:     http://github.com/veeam/blksnap
ExclusiveOS:    linux
ExclusiveArch:  %{ix86} x86_64

Source0: %{name}-%{version}.tar.gz

BuildRoot: %{_tmppath}/%{name}-buildroot

%description
This package provides the blksnap kernel module.
It is built to depend upon the specific ABI provided by a range of releases
of the same variant of the Linux kernel and not on any one specific build.

%package -n kmod-%{name}-patch
Summary: %{name} patch kernel module
Group: System Environment/Kernel
Requires: python3, kmod-blksnap = %{version}

%description -n kmod-%{name}-patch
This package provides the ${kmod_name} kernel modules built for the Linux
kernel %{kversion} for the %{_target_cpu} family of processors.

# Disable the building of the debug package(s).
%define debug_package %{nil}

%prep
%setup -q -n %{name}-%{version}

%build
for kver in %{kversion}; do
	KSRC=%{_usrsrc}/kernels/${kver}

	mkdir -p $PWD/${kver}
	cp -rf $PWD/module/ $PWD/${kver}/
	%{__make} -j$(nproc) -C "${KSRC}" %{?_smp_mflags} modules M=$PWD/module
done

for kver in %{kversion}; do
	KSRC=%{_usrsrc}/kernels/${kver}

	export INSTALL_MOD_PATH=%{buildroot}
	export INSTALL_MOD_DIR=extra

	%{__make} -C "${KSRC}" modules_install M=$PWD/module
	%{__rm} -f %{buildroot}/lib/modules/${kver}/modules.*
done

%files -n kmod-%{name}-patch
%defattr(644,root,root,755)
/lib/modules/

%clean
%{__rm} -rf %{buildroot}

%changelog
