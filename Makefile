
all: swidtags_plugin

swidtags_plugin: swidtags_plugin.so

swidtags_plugin.so: swidtags_plugin.c
	gcc -g -fPIC -shared $(shell pkgconf --cflags --libs libdnf libxml-2.0) -Wcast-align -Wno-uninitialized -Wredundant-decls -Wwrite-strings -Wformat-nonliteral -Wmissing-format-attribute -Wsign-compare -Wtype-limits -Wuninitialized -Wall -Werror=implicit-function-declaration -Wl,--as-needed -Wmissing-prototypes -Waggregate-return -Wshadow -o swidtags_plugin.so swidtags_plugin.c

test:
	./test.sh

install: swidtags_plugin.so
	cp swidtags_plugin.so /usr/lib*/libdnf/plugins/

clean:
	rm -rf $(shell cat .gitignore)

VERSION := $(shell rpm -q --qf '%{name}-%{version}\n' --specfile *.spec 2> /dev/null | head -1)

srpm:
	tar c --transform="s#^#$(VERSION)/#" --exclude-from=.dockerignore -vzf $(VERSION).tar.gz *
	rpmbuild -D '_srcrpmdir .' -D '_sourcedir .' -bs *.spec

.PHONY: swidtags_plugin install test clean

