Name: blk-snap
BuildRequires: %kernel_module_package_buildreqs
License: GPL-2.0
Packager: "#PACKAGE_VENDOR#"
Vendor: "#PACKAGE_VENDOR#"
Group: System/Kernel
Summary: Block device snapshot (kernel module)
Version: #PACKAGE_VERSION#
Release: 2%{?dist}
Source0: %{name}-%{version}.tar.gz
Source10: %{name}-%{version}-preamble
BuildRoot: %{_tmppath}/%{name}-%{version}-build
URL: http://github.com/veeam/%{name}
Requires: kernel-syms modutils

%kernel_module_package -n %{name} -p %{_sourcedir}/%{name}-%{version}-preamble

%description
This kernel module implements snapshot and changed block tracking
functionality.

%package KMP
Summary: Block device snapshot (binary kernel module package)
Group:	System/Kernel

%description KMP
This kernel module implements snapshot and changed block tracking
functionality.

%prep
%setup -q -n %{name}-%{version}
mkdir obj

%build
for flavor in %flavors_to_build; do
    rm -rf obj/$flavor
    cp -r source obj/$flavor
	%{__make} -C %{kernel_source $flavor} modules M=$PWD/obj/$flavor
done

%install
export INSTALL_MOD_PATH=$RPM_BUILD_ROOT
export INSTALL_MOD_DIR=updates/drivers
export INSTALL_MOD_STRIP=--strip-debug
for flavor in %flavors_to_build; do
	make -C %{kernel_source $flavor} modules_install M=$PWD/obj/$flavor
done

%clean
rm -rf %{buildroot}

%changelog
