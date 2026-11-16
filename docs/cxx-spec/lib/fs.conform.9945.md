# 29.11.2.1 POSIX conformance [fs.conform.9945]

[Contents](../README.md) · up: [29.11.2 Conformance \[fs.conformance\]](fs.conformance.md) · prev: [29.11.2 Conformance \[fs.conformance\]](fs.conformance.md) · next: [29.11.2.2 Operating system dependent behavior conformance \[fs.conform.os\]](fs.conform.os.md)

**1** Some behavior is specified by reference to POSIX ([[fs.norm.ref]](fs.norm.ref.md)). How such behavior is actually implemented is unspecified. [*Note 1*: This constitutes an “as if” rule allowing implementations to call native operating system or other APIs. — *end note*]

**2** Implementations should provide such behavior as it is defined by POSIX. Implementations shall document any behavior that differs from the behavior defined by POSIX. Implementations that do not support exact POSIX behavior should provide behavior as close to POSIX behavior as is reasonable given the limitations of actual operating systems and file systems. If an implementation cannot provide any reasonable behavior, the implementation shall report an error as specified in [[fs.err.report]](fs.err.report.md). [*Note 2*: This allows users to rely on an exception being thrown or an error code being set when an implementation cannot provide any reasonable behavior. — *end note*]

**3** Implementations are not required to provide behavior that is not supported by a particular file system. [*Example 1*: The FAT file system used by some memory cards, camera memory, and floppy disks does not support hard links, symlinks, and many other features of more capable file systems, so implementations are not required to support those features on the FAT file system but instead are required to report an error as described above. — *end example*]
