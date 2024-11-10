
all: swidtags_plugin

swidtags_plugin: dist/swidtags.so

dist/swidtags.so: swidtags_plugin.cpp
	rm -rf dist
	mkdir dist
	cmake -B dist
	make -C dist

test:
	./test.sh

install: dist/swidtags.so
	cp dist/swidtags.so /usr/lib*/libdnf5/plugins/

clean:
	rm -rf $(shell cat .gitignore)

VERSION := $(shell rpm -q --qf '%{name}-%{version}\n' --specfile *.spec 2> /dev/null | head -1)

srpm:
	tar c --transform="s#^#$(VERSION)/#" --exclude-from=.dockerignore -vzf $(VERSION).tar.gz *
	rpmbuild -D '_srcrpmdir .' -D '_sourcedir .' -bs *.spec

.PHONY: swidtags_plugin install test clean

