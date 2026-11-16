# 21.1 General [strings.general]

[Contents](../README.md) · up: [21 Strings library \[strings\]](strings.md) · prev: [21 Strings library \[strings\]](strings.md) · next: [21.2 Character traits \[char.traits\]](char.traits.md)

**1** This Clause describes components for manipulating sequences of any non-array trivial standard-layout ([[basic.types]](../basic.types.md)) type. Such types are called *char-like types*, and objects of char-like types are called *char-like objects* or simply *characters*.

**2** The following subclauses describe a character traits class, string classes, and null-terminated sequence utilities, as summarized in [Table 68](strings.general.md#tab:strings.summary).

<a id="tab:strings.summary"></a>
**Table 68: Strings library summary** [tab:strings.summary]

|  |  |  |
| --- | --- | --- |
| [[char.traits]](char.traits.md) | Character traits | `<string>` |
| [[string.classes]](string.classes.md) | String classes |  |
| [[string.view]](string.view.md) | String view classes | `<string_view>` |
| [[c.strings]](c.strings.md) | Null-terminated sequence utilities | `<cctype>`, `<cstdlib>`, `<cstring>`, `<cuchar>`, `<cwchar>`, `<cwctype>` |
