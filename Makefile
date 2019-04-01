
all: swidtags_plugin

swidtags_plugin: swidtags_plugin.so

swidtags_plugin.so: swidtags_plugin.c
	gcc -g -fPIC -shared -I/usr/include/libxml2 -I/usr/include/glib-2.0 -I/lib64/glib-2.0/include -Wcast-align -Wno-uninitialized -Wredundant-decls -Wwrite-strings -Wformat-nonliteral -Wmissing-format-attribute -Wsign-compare -Wtype-limits -Wuninitialized -Wall -Werror=implicit-function-declaration -Wl,--as-needed -Wmissing-prototypes -Waggregate-return -Wshadow -o swidtags_plugin.so swidtags_plugin.c

test:
	./test.sh

install: swidtags_plugin.so
	cp swidtags_plugin.so /usr/lib64/libdnf/plugins/

clean:
	rm -rf $(shell cat .gitignore)

VERSION := $(shell rpm -q --qf '%{name}-%{version}\n' --specfile *.spec 2> /dev/null | head -1)

srpm:
	mkdir -p .source/$(VERSION) && cp -rp * .source/$(VERSION) && cd .source && tar cvzf $(VERSION).tar.gz $(VERSION)
	rpmbuild -D '_srcrpmdir $(CURDIR)' -D '_sourcedir .source' -bs *.spec
	rm -rf .source

.PHONY: swidtags_plugin install test clean

