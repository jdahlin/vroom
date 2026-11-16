# 5.3 Character sets [lex.charset]

[Contents](../README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.2 Phases of translation \[lex.phases\]](lex.phases.md) · next: [5.4 Preprocessing tokens \[lex.pptoken\]](lex.pptoken.md)

**1** The *basic source character set* consists of 96 characters: the space character, the control characters representing horizontal tab, vertical tab, form feed, and new-line, plus the following 91 graphical characters:[^1]

```cpp
a b c d e f g h i j k l m n o p q r s t u v w x y z
A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
0 1 2 3 4 5 6 7 8 9
_ { } [ ] # ( ) < > % : ; . ? * + - / ^ & | ~ ! = , \ " '
```

**2** The *universal-character-name* construct provides a way to name other characters.

```bnf
hex-quad:
    hexadecimal-digit hexadecimal-digit hexadecimal-digit hexadecimal-digit
```

```bnf
universal-character-name:
    '\u' hex-quad
    '\U' hex-quad hex-quad
```

A *universal-character-name* designates the character in ISO/IEC 10646 (if any) whose code point is the hexadecimal number represented by the sequence of *hexadecimal-digit*s in the *universal-character-name*. The program is ill-formed if that number is not a code point or if it is a surrogate code point. Noncharacter code points and reserved code points are considered to designate separate characters distinct from any ISO/IEC 10646 character. If a *universal-character-name* outside the *c-char-sequence*, *s-char-sequence*, or *r-char-sequence* of a *character-literal* or *string-literal* (in either case, including within a *user-defined-literal*) corresponds to a control character or to a character in the basic source character set, the program is ill-formed.[^2] [*Note 1*: ISO/IEC 10646 code points are integers in the range [0, 10*FFFF*] (hexadecimal). A surrogate code point is a value in the range [*D*800, *DFFF*] (hexadecimal). A control character is a character whose code point is in either of the ranges [0, 1*F*] or [7*F*, 9*F*] (hexadecimal). — *end note*]

**3** The *basic execution character set* and the *basic execution wide-character set* shall each contain all the members of the basic source character set, plus control characters representing alert, backspace, and carriage return, plus a *null character* (respectively, *null wide character*), whose value is 0. For each basic execution character set, the values of the members shall be non-negative and distinct from one another. In both the source and execution basic character sets, the value of each character after `0` in the above list of decimal digits shall be one greater than the value of the previous. The *execution character set* and the *execution wide-character set* are implementation-defined supersets of the basic execution character set and the basic execution wide-character set, respectively. The values of the members of the execution character sets and the sets of additional members are locale-specific.

[^1]: The glyphs for the members of the basic source character set are intended to identify characters from the subset of ISO/IEC 10646 which corresponds to the ASCII character set. However, because the mapping from source file characters to the source character set (described in translation phase 1) is specified as implementation-defined, an implementation is required to document how the basic source characters are represented in source files.
[^2]: A sequence of characters resembling a *universal-character-name* in an *r-char-sequence* ([[lex.string]](lex.string.md)) does not form a *universal-character-name*.
