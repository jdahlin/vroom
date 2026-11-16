# 29.11.1 General [fs.general]

[Contents](../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11 File systems \[filesystems\]](filesystems.md) · next: [29.11.2 Conformance \[fs.conformance\]](fs.conformance.md)

**1** Subclause [[filesystems]](filesystems.md) describes operations on file systems and their components, such as paths, regular files, and directories.

**2** A *file system* is a collection of files and their attributes.

**3** A *file* is an object within a file system that holds user or system data. Files can be written to, or read from, or both. A file has certain attributes, including type. File types include regular files and directories. Other types of files, such as symbolic links, may be supported by the implementation.

**4** A *directory* is a file within a file system that acts as a container of directory entries that contain information about other files, possibly including other directory files. The *parent directory* of a directory is the directory that both contains a directory entry for the given directory and is represented by the dot-dot filename ([[fs.path.generic]](fs.path.generic.md)) in the given directory. The *parent directory* of other types of files is a directory containing a directory entry for the file under discussion.

**5** A *link* is an object that associates a filename with a file. Several links can associate names with the same file. A *hard link* is a link to an existing file. Some file systems support multiple hard links to a file. If the last hard link to a file is removed, the file itself is removed. [*Note 1*: A hard link can be thought of as a shared-ownership smart pointer to a file. — *end note*] A *symbolic link* is a type of file with the property that when the file is encountered during pathname resolution ([[fs.class.path]](fs.class.path.md)), a string stored by the file is used to modify the pathname resolution. [*Note 2*: Symbolic links are often called symlinks. A symbolic link can be thought of as a raw pointer to a file. If the file pointed to does not exist, the symbolic link is said to be a “dangling” symbolic link. — *end note*]
