
# The libdnf5 swidtags plugin

## Keeping SWID tags in sync with rpms installed via libdnf5-based tools

To keep the SWID information synchronized with SWID tags from dnf/yum
repository metadata for package installations, upgrades, and removals
using tools based on libdnf5 (for example microdnf), libdnf5 plugin
`swidtags.so` can be used. The plugin gets enabled by placing that
shared library to `/usr/lib64/libdnf5/plugins/` and adding
`/etc/dnf/libdnf5-plugins/swidtags.conf` with content

```
[main]
enabled = yes
```

To display debug messages from the `swidtags.so` plugin, set the
`LIBDNF_PLUGIN_SWIDTAGS_DEBUG` environment variable to desired debug
level, from 1 to 9.

## Author

Written by Jan Pazdziora, 2018--2024.

## License

Licensed under the GNU Lesser General Public License v2.1.
