FROM registry.fedoraproject.org/fedora:rawhide
RUN dnf install -y rpm-build cmake "dnf-command(builddep)" /usr/bin/dbus-daemon PackageKit polkit fakechroot /usr/bin/getopt
COPY . /src/
WORKDIR /src
RUN make -f Makefile.in srpm
RUN dnf builddep -y *.src.rpm
RUN rpm -Uvh *.src.rpm
RUN rpmbuild -ba /root/rpmbuild/SPECS/*.spec
RUN ln -s $( ls /root/rpmbuild/RPMS/*/*.x86_64.rpm | grep -v debug ) libdnf5-plugin-swidtags.rpm
