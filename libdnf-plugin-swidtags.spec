
# Running rpmbuild --with test requires network access
%bcond_with test

Summary: Keeping SWID tags in sync with rpms installed via libdnf5-based tools
Name: libdnf5-plugin-swidtags
Version: 5.0.0
Release: 0%{?dist}
URL: https://github.com/swidtags/%{name}
Source0: https://github.com/swidtags/libdnf-plugin-swidtags/releases/download/libdnf-plugin-swidtags-%{version}/%{name}-%{version}.tar.gz
License: LGPLv2

BuildRequires: pkgconf
BuildRequires: make
BuildRequires: cmake
BuildRequires: g++
BuildRequires: libdnf5-devel
BuildRequires: libxml++-devel
# The following BuildRequires are only needed for check
%if %{with test}
BuildRequires: dnf
BuildRequires: tar
%endif

Requires: libdnf5

%description
The libdnf5 plugin swidtags_plugin.so can be used to keep the SWID
information synchronized with SWID tags from dnf/yum repository
metadata for package installations, upgrades, and removals using
tools based on libdnf5 (dnf5, microdnf).

%prep
%setup -q
%build

%cmake
%cmake_build

%install

install -d %{buildroot}%{_libdir}/libdnf5/plugins
%cmake_install

%check
%if %{with test}
make test
%endif

%files
%doc README.md
%license LICENSE
%{_libdir}/libdnf5/plugins/swidtags.so
%{_sysconfdir}/dnf/libdnf5-plugins/swidtags.conf

