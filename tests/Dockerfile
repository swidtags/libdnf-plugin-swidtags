FROM registry.fedoraproject.org/fedora
RUN dnf install -y rpm-build make "dnf-command(builddep)" /usr/bin/dbus-daemon PackageKit polkit
COPY . /src/
WORKDIR /src
RUN make srpm
RUN dnf builddep -y *.src.rpm
RUN rpm -Uvh *.src.rpm
RUN rpmbuild -ba /root/rpmbuild/SPECS/*.spec
RUN ln -s $( ls /root/rpmbuild/RPMS/*/*.x86_64.rpm | grep -v debug ) libdnf-plugin-swidtags.rpm
RUN rm -f /usr/share/dbus-1/system.d/*
