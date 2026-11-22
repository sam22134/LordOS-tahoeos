This directory is a Debian 13 packaging placeholder. The active LordOS package and ISO pipeline is in `tools/iso/mkdeb.sh` and `tools/iso/build.sh`.

The pinned MacTahoe freedesktop icon theme can be installed into a system or image staging root with `bash packaging/icons/install-mactahoe.sh`. See [packaging/icons/README.md](../icons/README.md); the active `tools/iso/mkdeb.sh` package builder already calls this installer, and the live ISO installs that package.
