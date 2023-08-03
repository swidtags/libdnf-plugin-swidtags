#!/bin/bash

set -e
set -x

# Test dnf plugin

export LIBDNF_PLUGIN_SWIDTAGS_DEBUG=10

if [ "$TEST_INSTALLED" = true ] ; then
	MICRODNF_ROOT=/
	BIN=/usr/bin
	rpm -q microdnf PackageKit || dnf install -y microdnf PackageKit
else
	MICRODNF_ROOT=$(pwd)/tmp/microdnfroot/
	rm -rf $MICRODNF_ROOT
	mkdir -p ${MICRODNF_ROOT}var/cache

	if [ "$UID" != 0 ] ; then
		FAKEROOT=fakeroot
		FAKECHROOT=fakechroot
	fi
	RUN_MICRODNF="$FAKECHROOT chroot $MICRODNF_ROOT"

	cp -rp /var/cache/dnf ${MICRODNF_ROOT}var/cache
	$FAKEROOT dnf --releasever "$( rpm --qf '%{version}\n' -q --whatprovides system-release )" --installroot $MICRODNF_ROOT --setopt=tsflags=noscripts --noplugins install --downloadonly -y filesystem
	( cd $MICRODNF_ROOT && find var/cache/dnf -name 'filesystem*.rpm' | xargs rpm2cpio | cpio -id )
	chmod -R u+w $MICRODNF_ROOT
	$FAKECHROOT $FAKEROOT dnf --releasever "$( rpm --qf '%{version}\n' -q --whatprovides system-release )" --installroot $MICRODNF_ROOT --setopt=tsflags=noscripts --disableplugin='*' install -y microdnf

	if ! [ -f ${MICRODNF_ROOT}usr/lib/os-release ] ; then
		cp /usr/lib/os-release ${MICRODNF_ROOT}usr/lib/
	fi
	if [ -z "$FAKEROOT" ] ; then
		cp /etc/resolv.conf ${MICRODNF_ROOT}etc/
	fi
	if [ -n "$FAKECHROOT" ] ; then
		(
		cd ${MICRODNF_ROOT}usr/lib
		ln -sf ../lib*/libpeas-1.0.so.0 libpeas-1.0.so.0
		ln -sf ../lib*/libgirepository-1.0.so.1 libgirepository-1.0.so.1
		)
	fi

	$RUN_MICRODNF update-ca-trust

	cp swidtags_plugin.so ${MICRODNF_ROOT}usr/lib*/libdnf/plugins/
fi

$RUN_MICRODNF microdnf install -y zsh
$RUN_MICRODNF microdnf remove -y zsh

cp tests/test-libdnf-swidtags.repo ${MICRODNF_ROOT}etc/yum.repos.d/
rm -rf ${MICRODNF_ROOT}repo
cp -rp tests/repo ${MICRODNF_ROOT}repo
$RUN_MICRODNF microdnf install -y hello-2.0
ls -la ${MICRODNF_ROOT}usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag
echo "528f1308b12cc7770f0d26451e5511f7099695f3c343e97d1c4b5b1fea47563f ${MICRODNF_ROOT}usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag" | sha256sum -c
$RUN_MICRODNF microdnf remove -y hello
( ! test -f ${MICRODNF_ROOT}usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag)

if [ "$TEST_INSTALLED" = true ] ; then
	pkcon install -y require-old-hello
	ls -la /usr/lib/swidtag/example^2ftest/example^2ftest.require-old-hello-1.0-1.fc32.noarch-rpm-ed106bd08fab18df2da50441a68e836984ac0e95b909bba5fa1ae87d1e30f0ed.swidtag
	ls -la /usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag
	echo "528f1308b12cc7770f0d26451e5511f7099695f3c343e97d1c4b5b1fea47563f /usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag" | sha256sum -c
	pkcon remove -y hello
	( ! test -f /usr/lib/swidtag/example^2ftest/example^2ftest.hello-2.0-1.x86_64-rpm-ef920781af3bf072ae9888eec3de1c589143101dff9cc0b561468d395fb766d9.swidtag)
	( ! test -f /usr/lib/swidtag/example^2ftest/example^2ftest.require-old-hello-1.0-1.fc32.noarch-rpm-ed106bd08fab18df2da50441a68e836984ac0e95b909bba5fa1ae87d1e30f0ed.swidtag)
fi

echo OK $0.

