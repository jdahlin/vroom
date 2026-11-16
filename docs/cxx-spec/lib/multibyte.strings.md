# 16.4.2.2.5.2 Multibyte strings [multibyte.strings]

[Contents](../README.md) · up: [16.4.2.2.5 Character sequences \[character.seq\]](character.seq.md) · prev: [16.4.2.2.5.1 Byte strings \[byte.strings\]](byte.strings.md) · next: [16.4.2.2.6 Customization Point Object types \[customization.point.object\]](customization.point.object.md)

**1** A *null-terminated multibyte string*, or NTMBS, is an NTBS that constitutes a sequence of valid multibyte characters, beginning and ending in the initial shift state.[^1]

**2** A *static NTMBS* is an NTMBS with static storage duration.

[^1]: An NTBS that contains characters only from the basic execution character set is also an NTMBS. Each multibyte character then consists of a single byte.
