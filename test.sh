#!/bin/bash

set -e
set -x

# Test dnf plugin

export LIBDNF_PLUGIN_SWIDTAGS_DEBUG=10

MICRODNF_ROOT=$(pwd)/tmp/microdnfroot
rm -rf $MICRODNF_ROOT
mkdir -p $MICRODNF_ROOT/var/cache

RUN_MICRODNF="chroot $MICRODNF_ROOT"

cp -rp /var/cache/dnf $MICRODNF_ROOT/var/cache
dnf --releasever "$( rpm --qf '%{version}\n' -q --whatprovides system-release )" --installroot $MICRODNF_ROOT --setopt=tsflags=noscripts --noplugins install --downloadonly -y filesystem
( cd $MICRODNF_ROOT && find var/cache/dnf -name 'filesystem*.rpm' | xargs rpm2cpio | cpio -id )
chmod -R u+w $MICRODNF_ROOT
dnf --releasever "$( rpm --qf '%{version}\n' -q --whatprovides system-release )" --installroot $MICRODNF_ROOT --setopt=tsflags=noscripts --disableplugin='*' install -y microdnf

cp swidtags_plugin.so $MICRODNF_ROOT/usr/lib64/libdnf/plugins/
$RUN_MICRODNF microdnf install zsh
$RUN_MICRODNF microdnf remove zsh

cp tests/test-libdnf-swidtags.repo $MICRODNF_ROOT/etc/yum.repos.d/
cp -rp tests/repo $MICRODNF_ROOT/repo
$RUN_MICRODNF microdnf install hello
ls -la $MICRODNF_ROOT/usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag
echo "528f1308b12cc7770f0d26451e5511f7099695f3c343e97d1c4b5b1fea47563f $MICRODNF_ROOT/usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag" | sha256sum -c
$RUN_MICRODNF microdnf remove hello
( ! test -f $MICRODNF_ROOT/usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag)

echo OK $0.

