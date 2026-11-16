# 29.11.9.5 Enum class `perm_options` [fs.enum.perm.opts]

[Contents](../README.md) · up: [29.11.9 Enumerations \[fs.enum\]](fs.enum.md) · prev: [29.11.9.4 Enum class `perms` \[fs.enum.perms\]](fs.enum.perms.md) · next: [29.11.9.6 Enum class `directory_options` \[fs.enum.dir.opts\]](fs.enum.dir.opts.md)

**1** The `enum class` type `perm_options` is a bitmask type ([[bitmask.types]](bitmask.types.md)) that specifies bitmask constants used to control the semantics of permissions operations, with the meanings listed in [Table 132](fs.enum.perm.opts.md#tab:fs.enum.perm.opts). The bitmask constants are bitmask elements. In [Table 132](fs.enum.perm.opts.md#tab:fs.enum.perm.opts) `perm` denotes a value of type `perms` passed to `permissions`.

<a id="tab:fs.enum.perm.opts"></a>
**Table 132: Enum class `perm_options`** [tab:fs.enum.perm.opts]

| Name | Meaning |
| --- | --- |
| `replace` | `permissions` shall replace the file’s permission bits with `perm` |
| `add` | `permissions` shall replace the file’s permission bits with the bitwise OR of `perm` and the file’s current permission bits. |
| `remove` | `permissions` shall replace the file’s permission bits with the bitwise AND of the complement of `perm` and the file’s current permission bits. |
| `nofollow` | `permissions` shall change the permissions of a symbolic link itself rather than the permissions of the file the link resolves to. |
