# 29.11.9.2 Enum class `file_type` [fs.enum.file.type]

[Contents](../README.md) · up: [29.11.9 Enumerations \[fs.enum\]](fs.enum.md) · prev: [29.11.9.1 Enum `path::format` \[fs.enum.path.format\]](fs.enum.path.format.md) · next: [29.11.9.3 Enum class `copy_options` \[fs.enum.copy.opts\]](fs.enum.copy.opts.md)

**1** This enum class specifies constants used to identify file types, with the meanings listed in [Table 129](fs.enum.file.type.md#tab:fs.enum.file.type). The values of the constants are distinct.

<a id="tab:fs.enum.file.type"></a>
**Table 129: Enum class `file_type`** [tab:fs.enum.file.type]

| Constant | Meaning |
| --- | --- |
| `none` | The type of the file has not been determined or an error occurred while trying to determine the type. |
| `not_found` | Pseudo-type indicating the file was not found. [*Note 1*: The file not being found is not considered an error while determining the type of a file. — *end note*] |
| `regular` | Regular file |
| `directory` | Directory file |
| `symlink` | Symbolic link file |
| `block` | Block special file |
| `character` | Character special file |
| `fifo` | FIFO or pipe file |
| `socket` | Socket file |
| `implementation-defined` | Implementations that support file systems having file types in addition to the above `file_type` types shall supply implementation-defined `file_type` constants to separately identify each of those additional file types |
| `unknown` | The file exists but the type could not be determined |
