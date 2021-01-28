Plugins for use with [HTSlib]. Note the `HTS_PATH` environment variable must be set to the plugin directory in order for programs compiled with htslib to find the plugins. See the samtools.1 man page for details.

### EGA-style encrypted (.cip) files

The _hfile_cip_ plugin provides access to files encrypted with the
[European Genome-Phenome Archive][EGA]'s AES-CTR scheme, which usually
have the extension _.cip_.
The en-/decryption key is taken from the `$HTS_CIP_KEY` environment variable.

### iRODS

The _hfile_irods_ plugin provides access to remote data stored in [iRODS].
It can be built for iRODS 3.x, 4.1.x, or 4.2 onwards, and the resulting
plugins can be renamed with version numbers so that they can be installed
alongside each other.

When built against iRODS 4.1.x, the plugin is incompatible with
HTSlib 1.3.1 and earlier as it needs to be loaded with `RTLD_GLOBAL`.
The _hfile_irods_wrapper_ plugin can be installed in the same directory
as _hfile_irods_ to work around this problem and enable the iRODS plugin
to be used with these earlier versions of HTSlib.

### Memory-mapped local files

The _hfile_mmap_ plugin provides access to local files via `mmap(2)`.


[EGA]:    https://ega-archive.org/
[HTSlib]: https://github.com/samtools/htslib
[iRODS]:  http://irods.org/
