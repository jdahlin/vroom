# 29.11.14 Filesystem operation functions [fs.op.funcs]

[Contents](../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.13.2 Non-member functions \[fs.rec.dir.itr.nonmembers\]](fs.rec.dir.itr.nonmembers.md) · next: [29.11.14.1 Absolute \[fs.op.absolute\]](fs.op.absolute.md)

**1** Filesystem operation functions query or modify files, including directories, in external storage.

**2** [*Note 1*: Because hardware failures, network failures, file system races ([[fs.race.behavior]](fs.race.behavior.md)), and many other kinds of errors occur frequently in file system operations, users should be aware that any filesystem operation function, no matter how apparently innocuous, may encounter an error; see [[fs.err.report]](fs.err.report.md). — *end note*]

## Subclauses

- [29.11.14.1 Absolute \[fs.op.absolute\]](fs.op.absolute.md)
- [29.11.14.2 Canonical \[fs.op.canonical\]](fs.op.canonical.md)
- [29.11.14.3 Copy \[fs.op.copy\]](fs.op.copy.md)
- [29.11.14.4 Copy file \[fs.op.copy.file\]](fs.op.copy.file.md)
- [29.11.14.5 Copy symlink \[fs.op.copy.symlink\]](fs.op.copy.symlink.md)
- [29.11.14.6 Create directories \[fs.op.create.directories\]](fs.op.create.directories.md)
- [29.11.14.7 Create directory \[fs.op.create.directory\]](fs.op.create.directory.md)
- [29.11.14.8 Create directory symlink \[fs.op.create.dir.symlk\]](fs.op.create.dir.symlk.md)
- [29.11.14.9 Create hard link \[fs.op.create.hard.lk\]](fs.op.create.hard.lk.md)
- [29.11.14.10 Create symlink \[fs.op.create.symlink\]](fs.op.create.symlink.md)
- [29.11.14.11 Current path \[fs.op.current.path\]](fs.op.current.path.md)
- [29.11.14.12 Equivalent \[fs.op.equivalent\]](fs.op.equivalent.md)
- [29.11.14.13 Exists \[fs.op.exists\]](fs.op.exists.md)
- [29.11.14.14 File size \[fs.op.file.size\]](fs.op.file.size.md)
- [29.11.14.15 Hard link count \[fs.op.hard.lk.ct\]](fs.op.hard.lk.ct.md)
- [29.11.14.16 Is block file \[fs.op.is.block.file\]](fs.op.is.block.file.md)
- [29.11.14.17 Is character file \[fs.op.is.char.file\]](fs.op.is.char.file.md)
- [29.11.14.18 Is directory \[fs.op.is.directory\]](fs.op.is.directory.md)
- [29.11.14.19 Is empty \[fs.op.is.empty\]](fs.op.is.empty.md)
- [29.11.14.20 Is fifo \[fs.op.is.fifo\]](fs.op.is.fifo.md)
- [29.11.14.21 Is other \[fs.op.is.other\]](fs.op.is.other.md)
- [29.11.14.22 Is regular file \[fs.op.is.regular.file\]](fs.op.is.regular.file.md)
- [29.11.14.23 Is socket \[fs.op.is.socket\]](fs.op.is.socket.md)
- [29.11.14.24 Is symlink \[fs.op.is.symlink\]](fs.op.is.symlink.md)
- [29.11.14.25 Last write time \[fs.op.last.write.time\]](fs.op.last.write.time.md)
- [29.11.14.26 Permissions \[fs.op.permissions\]](fs.op.permissions.md)
- [29.11.14.27 Proximate \[fs.op.proximate\]](fs.op.proximate.md)
- [29.11.14.28 Read symlink \[fs.op.read.symlink\]](fs.op.read.symlink.md)
- [29.11.14.29 Relative \[fs.op.relative\]](fs.op.relative.md)
- [29.11.14.30 Remove \[fs.op.remove\]](fs.op.remove.md)
- [29.11.14.31 Remove all \[fs.op.remove.all\]](fs.op.remove.all.md)
- [29.11.14.32 Rename \[fs.op.rename\]](fs.op.rename.md)
- [29.11.14.33 Resize file \[fs.op.resize.file\]](fs.op.resize.file.md)
- [29.11.14.34 Space \[fs.op.space\]](fs.op.space.md)
- [29.11.14.35 Status \[fs.op.status\]](fs.op.status.md)
- [29.11.14.36 Status known \[fs.op.status.known\]](fs.op.status.known.md)
- [29.11.14.37 Symlink status \[fs.op.symlink.status\]](fs.op.symlink.status.md)
- [29.11.14.38 Temporary directory path \[fs.op.temp.dir.path\]](fs.op.temp.dir.path.md)
- [29.11.14.39 Weakly canonical \[fs.op.weakly.canonical\]](fs.op.weakly.canonical.md)
