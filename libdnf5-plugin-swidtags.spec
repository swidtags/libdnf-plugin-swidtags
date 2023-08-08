
# Running rpmbuild --with test requires network access
%bcond_with test

Summary: Keeping SWID tags in sync with rpms installed via libdnf5-based tools
Name: libdnf5-plugin-swidtags
Version: 5.0.0
Release: 1%{?dist}
URL: https://github.com/swidtags/%{name}
Source0: https://github.com/swidtags/libdnf-plugin-swidtags/releases/download/libdnf-plugin-swidtags-%{version}/libdnf-plugin-swidtags-%{version}.tar.gz
License: LGPLv2

BuildRequires: pkgconf
BuildRequires: cmake
BuildRequires: g++
BuildRequires: libdnf5-devel
BuildRequires: libxml++-devel
# The following BuildRequires are only needed for check
%if %{with test}
BuildRequires: dnf5
BuildRequires: tar
%endif

Requires: libdnf5

%description
The libdnf5 plugin swidtags_plugin.so can be used to keep the SWID
information synchronized with SWID tags from dnf/yum repository
metadata for package installations, upgrades, and removals using
tools based on libdnf5 (dnf5, microdnf).

%prep
%setup -q -n libdnf-plugin-swidtags-%{version}
%build

%cmake
%cmake_build

%install

install -d %{buildroot}%{_libdir}/libdnf5/plugins
%cmake_install

%check
%if %{with test}
./test.sh
%endif

%files
%doc README.md
%license LICENSE
%{_libdir}/libdnf5/plugins/swidtags.so
%{_sysconfdir}/dnf/libdnf5-plugins/swidtags.conf

%changelog
* Thu May 21 2020 Jan Pazdziora <jpazdziora@redhat.com> - 0.8.8-1
- Test fixes.

* Wed Jun 05 2019 Jan Pazdziora <jpazdziora@redhat.com> - 0.8.7-1
- Build and rpm dependency improvements.

* Tue Jun 04 2019 Jan Pazdziora <jpazdziora@redhat.com> - 0.8.6-1
- Make compatible with PackageKit.
- Fix memory leaks.

* Mon May 27 2019 Jan Pazdziora <jpazdziora@redhat.com> - 0.8.5-1
- 1711989 - bring comments from Fedora package review upstream.

* Tue May 21 2019 Jan Pazdziora <jpazdziora@redhat.com> - 0.8.4-1
- Initial release.
