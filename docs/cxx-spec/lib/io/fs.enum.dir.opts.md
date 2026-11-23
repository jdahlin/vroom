# 29.11.9.6 Enum class `directory_options` [fs.enum.dir.opts]

[Contents](../../README.md) · up: [29.11.9 Enumerations \[fs.enum\]](fs.enum.md) · prev: [29.11.9.5 Enum class `perm_options` \[fs.enum.perm.opts\]](fs.enum.perm.opts.md) · next: [29.11.10 Class `file_status` \[fs.class.file.status\]](fs.class.file.status.md)

**1** The `enum class` type `directory_options` is a bitmask type ([[bitmask.types]](../library/bitmask.types.md)) that specifies bitmask constants used to identify directory traversal options, with the meanings listed in [Table 133](fs.enum.dir.opts.md#tab:fs.enum.dir.opts). The constant `none` represents the empty bitmask; every other constant in the table represents a distinct bitmask element.

<a id="tab:fs.enum.dir.opts"></a>
**Table 133: Enum class `directory_options`** [tab:fs.enum.dir.opts]

| Name | Meaning |
| --- | --- |
| `none` | (Default) Skip directory symlinks, permission denied is an error. |
| `follow_directory_symlink` | Follow rather than skip directory symlinks. |
| `skip_permission_denied` | Skip directories that would otherwise result in permission denied. |
