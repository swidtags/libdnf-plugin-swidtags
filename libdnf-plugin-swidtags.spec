
# Running rpmbuild --with test requires network access
%bcond_with test

Summary: Keeping SWID tags in sync with rpms installed via libdnf-based tools
Name: libdnf-plugin-swidtags
Version: 0.8.8
Release: 1%{?dist}
URL: https://github.com/swidtags/%{name}
Source0: https://github.com/swidtags/%{name}/releases/download/%{name}-%{version}/%{name}-%{version}.tar.gz
License: LGPLv2

BuildRequires: pkgconf
BuildRequires: make
BuildRequires: gcc
BuildRequires: libdnf-devel
# The following BuildRequires are only needed for check
%if %{with test}
BuildRequires: dnf
BuildRequires: tar
%endif

Requires: libdnf >= 0.24.1

%description
The libdnf plugin swidtags_plugin.so can be used to keep the SWID
information synchronized with SWID tags from dnf/yum repository
metadata for package installations, upgrades, and removals using
tools based on libdnf (for example microdnf).

%prep
%setup -q
%build

make %{?_smp_mflags} swidtags_plugin

%install

install -d %{buildroot}%{_libdir}/libdnf/plugins
install -m 755 swidtags_plugin.so %{buildroot}%{_libdir}/libdnf/plugins/

%check
%if %{with test}
make test
%endif

%files
%doc README.md
%license LICENSE
%{_libdir}/libdnf/plugins/swidtags_plugin.so

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
