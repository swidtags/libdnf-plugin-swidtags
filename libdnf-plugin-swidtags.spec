
Summary: Keeping SWID tags in sync with rpms installed via libdnf-based tools
Name: libdnf-plugin-swidtags
Version: 0.1.0
Release: 1%{?dist}
URL: https://github.com/swidtags/%{name}
Source0: https://github.com/swidtags/%{name}/releases/download/%{name}-%{version}/%{name}-%{version}.tar.gz
License: LGPLv2

BuildRequires: pkgconf
BuildRequires: make
BuildRequires: gcc
BuildRequires: libdnf-devel
# The following BuildRequires are only needed for check
BuildRequires: dnf
BuildRequires: tar

Requires: libdnf

%description
The libdnf plugin swidtags_plugin.so can be used to keep the SWID
information synchronized with SWID tags from dnf/yum repository
metadata for package installations, upgrades, and removals using
tools based on libdnf (for example microdnf).

%prep
%setup -q
%build

make swidtags_plugin

%install

install -d %{buildroot}%{_libdir}/libdnf/plugins
install -m 755 swidtags_plugin.so %{buildroot}%{_libdir}/libdnf/plugins/

%check
# make test

%files
%{_libdir}/libdnf/plugins/swidtags_plugin.so
