Summary: acf library
Name: acf
Version: 0
Release: 0
License: AppNexus, Inc.
Group: Applications/Internet
Source: libacf-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
Vendor: AppNexus, Inc.
Packager: Mobai Zhang <mzhang@appnexus.com>

BuildRequires: cmake >= 2.8.0

%if 0%{?rhel} < 6
BuildRequires: gcc44, gcc44-c++
%else
BuildRequires: gcc, gcc-c++
%endif

%description
This is shared C code for the real-time platform at AppNexus.

%prep

%setup -q
rm -rf %{buildroot}

%build

%if 0%{?rhel} < 6
%define _CC gcc44
%define _CXX g++44
%else
%define _CC gcc
%define _CXX g++
%endif

mkdir build
cd build
CC=%{_CC} CXX=%{_CXX} cmake -DDISABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RELWITHDEBINFO -DCMAKE_INSTALL_PREFIX:PATH=/usr -DCMAKE_ACF_VERSION=%{version} ..
make
cd ..;

%install
cd build && make install DESTDIR=%{buildroot}

%files
%defattr(-,root,root)
/usr/include/acf/*.h
/usr/lib64/libacf.*
/usr/lib64/pkgconfig/acf.pc
