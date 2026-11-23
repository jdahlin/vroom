# 29.11.9.1 Enum `path::format` [fs.enum.path.format]

[Contents](../../README.md) · up: [29.11.9 Enumerations \[fs.enum\]](fs.enum.md) · prev: [29.11.9 Enumerations \[fs.enum\]](fs.enum.md) · next: [29.11.9.2 Enum class `file_type` \[fs.enum.file.type\]](fs.enum.file.type.md)

**1** This enum specifies constants used to identify the format of the character sequence, with the meanings listed in [Table 128](fs.enum.path.format.md#tab:fs.enum.path.format).

<a id="tab:fs.enum.path.format"></a>
**Table 128: Enum `path::format`** [tab:fs.enum.path.format]

| Name | Meaning |
| --- | --- |
| `native_format` | The native pathname format. |
| `generic_format` | The generic pathname format. |
| `auto_format` | The interpretation of the format of the character sequence is implementation-defined. The implementation may inspect the content of the character sequence to determine the format. [*Note 1*: For POSIX-based systems, native and generic formats are equivalent and the character sequence should always be interpreted in the same way. — *end note*] |
