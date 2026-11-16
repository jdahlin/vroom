# 29.11.9.4 Enum class `perms` [fs.enum.perms]

[Contents](../README.md) · up: [29.11.9 Enumerations \[fs.enum\]](fs.enum.md) · prev: [29.11.9.3 Enum class `copy_options` \[fs.enum.copy.opts\]](fs.enum.copy.opts.md) · next: [29.11.9.5 Enum class `perm_options` \[fs.enum.perm.opts\]](fs.enum.perm.opts.md)

**1** The `enum class` type `perms` is a bitmask type ([[bitmask.types]](bitmask.types.md)) that specifies bitmask constants used to identify file permissions, with the meanings listed in [Table 131](fs.enum.perms.md#tab:fs.enum.perms).

<a id="tab:fs.enum.perms"></a>
**Table 131: Enum class `perms`** [tab:fs.enum.perms]

| Name | Value | POSIX | Definition or notes |
| --- | --- | --- | --- |
|  | (octal) | macro |  |
| `none` | `0` |  | There are no permissions set for the file. |
| `owner_read` | `0400` | `S_IRUSR` | Read permission, owner |
| `owner_write` | `0200` | `S_IWUSR` | Write permission, owner |
| `owner_exec` | `0100` | `S_IXUSR` | Execute/search permission, owner |
| `owner_all` | `0700` | `S_IRWXU` | Read, write, execute/search by owner;<br>`owner_read \| owner_write \| owner_exec` |
| `group_read` | `040` | `S_IRGRP` | Read permission, group |
| `group_write` | `020` | `S_IWGRP` | Write permission, group |
| `group_exec` | `010` | `S_IXGRP` | Execute/search permission, group |
| `group_all` | `070` | `S_IRWXG` | Read, write, execute/search by group;<br>`group_read \| group_write \| group_exec` |
| `others_read` | `04` | `S_IROTH` | Read permission, others |
| `others_write` | `02` | `S_IWOTH` | Write permission, others |
| `others_exec` | `01` | `S_IXOTH` | Execute/search permission, others |
| `others_all` | `07` | `S_IRWXO` | Read, write, execute/search by others;<br>`others_read \| others_write \| others_exec` |
| `all` | `0777` |  | `owner_all \| group_all \| others_all` |
| `set_uid` | `04000` | `S_ISUID` | Set-user-ID on execution |
| `set_gid` | `02000` | `S_ISGID` | Set-group-ID on execution |
| `sticky_bit` | `01000` | `S_ISVTX` | Operating system dependent. |
| `mask` | `07777` |  | `all \| set_uid \| set_gid \| sticky_bit` |
| `unknown` | `0xFFFF` |  | The permissions are not known, such as when a `file_status` object is created without specifying the permissions |
