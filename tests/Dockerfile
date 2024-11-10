FROM registry.fedoraproject.org/fedora:41
RUN dnf install -y rpm-build make fakechroot /usr/bin/getopt
COPY . /src/
WORKDIR /src
RUN make srpm
RUN dnf builddep -y *.src.rpm
RUN rpm -Uvh *.src.rpm
RUN rpmbuild -ba /root/rpmbuild/SPECS/*.spec
RUN ln -s $( ls /root/rpmbuild/RPMS/*/*.x86_64.rpm | grep -v debug ) libdnf5-plugin-swidtags.rpm
