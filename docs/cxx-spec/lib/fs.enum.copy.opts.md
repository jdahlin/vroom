# 29.11.9.3 Enum class `copy_options` [fs.enum.copy.opts]

[Contents](../README.md) · up: [29.11.9 Enumerations \[fs.enum\]](fs.enum.md) · prev: [29.11.9.2 Enum class `file_type` \[fs.enum.file.type\]](fs.enum.file.type.md) · next: [29.11.9.4 Enum class `perms` \[fs.enum.perms\]](fs.enum.perms.md)

**1** The `enum class` type `copy_options` is a bitmask type ([[bitmask.types]](bitmask.types.md)) that specifies bitmask constants used to control the semantics of copy operations. The constants are specified in option groups with the meanings listed in [Table 130](fs.enum.copy.opts.md#tab:fs.enum.copy.opts). The constant `none` represents the empty bitmask, and is shown in each option group for purposes of exposition; implementations shall provide only a single definition. Every other constant in the table represents a distinct bitmask element.

<a id="tab:fs.enum.copy.opts"></a>
**Table 130: Enum class `copy_options`** [tab:fs.enum.copy.opts]

|  |  |
| --- | --- |
| Option group controlling `copy_file` function effects for existing target files |  |
| Constant | Meaning |
| `none` | (Default) Error; file already exists. |
| `skip_existing` | Do not overwrite existing file, do not report an error. |
| `overwrite_existing` | Overwrite the existing file. |
| `update_existing` | Overwrite the existing file if it is older than the replacement file. |
| Option group controlling `copy` function effects for sub-directories |  |
| Constant | Meaning |
| `none` | (Default) Do not copy sub-directories. |
| `recursive` | Recursively copy sub-directories and their contents. |
| Option group controlling `copy` function effects for symbolic links |  |
| Constant | Meaning |
| `none` | (Default) Follow symbolic links. |
| `copy_symlinks` | Copy symbolic links as symbolic links rather than copying the files that they point to. |
| `skip_symlinks` | Ignore symbolic links. |
| Option group controlling `copy` function effects for choosing the form of copying |  |
| Constant | Meaning |
| `none` | (Default) Copy content. |
| `directories_only` | Copy directory structure only, do not copy non-directory files. |
| `create_symlinks` | Make symbolic links instead of copies of files. The source path shall be an absolute path unless the destination path is in the current directory. |
| `create_hard_links` | Make hard links instead of copies of files. |
