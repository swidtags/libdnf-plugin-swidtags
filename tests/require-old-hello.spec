
Summary: Require hello = 2.0
Name: require-old-hello
Version: 1.0
Release: 1%{?dist}
License: LGPLv2
BuildArch: noarch

Requires: hello = 2.0

%description
Since pkcon install cannot specify package versions,
we will pull our known version of hello package via dependency.

%files
