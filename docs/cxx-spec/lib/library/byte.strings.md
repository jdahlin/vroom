# 16.4.2.2.5.1 Byte strings [byte.strings]

[Contents](../../README.md) · up: [16.4.2.2.5 Character sequences \[character.seq\]](character.seq.md) · prev: [16.4.2.2.5 Character sequences \[character.seq\]](character.seq.md) · next: [16.4.2.2.5.2 Multibyte strings \[multibyte.strings\]](multibyte.strings.md)

**1** A *null-terminated byte string*, or NTBS, is a character sequence whose highest-addressed element with defined content has the value zero (the *terminating null character*); no other element in the sequence has the value zero.[^1]

**2** The *length of an NTBS* is the number of elements that precede the terminating null character. An *empty NTBS* has a length of zero.

**3** The *value of an NTBS* is the sequence of values of the elements up to and including the terminating null character.

**4** A *static NTBS* is an NTBS with static storage duration.[^2]

[^1]: Many of the objects manipulated by function signatures declared in `<cstring>` ([[cstring.syn]](../strings/cstring.syn.md)) are character sequences or NTBSs. The size of some of these character sequences is limited by a length value, maintained separately from the character sequence.
[^2]: A *string-literal*, such as `"abc"`, is a static NTBS.
