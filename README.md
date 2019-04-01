
# The libdnf swidtags plugin

## Keeping SWID tags in sync with rpms installed via libdnf-based tools

To keep the SWID information synchronized with SWID tags from dnf/yum
repository metadata for package installations, upgrades, and removals
using tools based on libdnf (for example microdnf), libdnf plugin
`swidtags_plugin.so` can be used. The plugin gets enabled by placing
that shared library to `/usr/lib64/libdnf/plugins/`.

To display debug messages from `swidtags_plugin.so`, set the
`LIBDNF_PLUGIN_SWIDTAGS_DEBUG` environment variable to desired debug
level, from 1 to 9.

## Author

Written by Jan Pazdziora, 2018--2019.

## License

Copyright 2018--2019, Red Hat, Inc.

Licensed under the GNU Lesser General Public License v2.1.
