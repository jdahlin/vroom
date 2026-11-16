# 29.11.7.1 Generic pathname format [fs.path.generic]

[Contents](../README.md) · up: [29.11.7 Class `path` \[fs.class.path\]](fs.class.path.md) · prev: [29.11.7 Class `path` \[fs.class.path\]](fs.class.path.md) · next: [29.11.7.2 Conversions \[fs.path.cvt\]](fs.path.cvt.md)

```bnf
pathname:
    root-nameₒₚₜ root-directoryₒₚₜ relative-path
```

```bnf
root-name:
    operating system dependent sequences of characters
    implementation-defined sequences of characters
```

```bnf
root-directory:
    directory-separator
```

```bnf
relative-path:
    filename
    filename directory-separator relative-path
    an empty path
```

```bnf
filename:
    non-empty sequence of characters other than directory-separator characters
```

```bnf
directory-separator:
    preferred-separator directory-separatorₒₚₜ
    fallback-separator directory-separatorₒₚₜ
```

```bnf
preferred-separator:
    operating system dependent directory separator character
```

```bnf
fallback-separator:
    '/', if preferred-separator is not '/'
```

**1** A *filename* is the name of a file. The *dot* and *dot-dot* filenames, consisting solely of one and two period characters respectively, have special meaning. The following characteristics of filenames are operating system dependent:

- The permitted characters. [*Example 1*: Some operating systems prohibit the ASCII control characters (0x00 – 0x1F) in filenames. — *end example*] [*Note 1*: For wide portability, users may wish to limit *filename* characters to the POSIX Portable Filename Character Set: <br> `A B C D E F G H I J K L M N O P Q R S T U V W X Y Z` <br> `a b c d e f g h i j k l m n o p q r s t u v w x y z` <br> `0 1 2 3 4 5 6 7 8 9. _ -` — *end note*]
- The maximum permitted length.
- Filenames that are not permitted.
- Filenames that have special meaning.
- Case awareness and sensitivity during path resolution.
- Special rules that may apply to file types other than regular files, such as directories.

**2** Except in a *root-name*, multiple successive *directory-separator* characters are considered to be the same as one *directory-separator* character.

**3** The dot filename is treated as a reference to the current directory. The dot-dot filename is treated as a reference to the parent directory. What the dot-dot filename refers to relative to *root-directory* is implementation-defined. Specific filenames may have special meanings for a particular operating system.

**4** A *root-name* identifies the starting location for pathname resolution ([[fs.class.path]](fs.class.path.md)). If there are no operating system dependent *root-name*s, at least one implementation-defined *root-name* is required. [*Note 2*: Many operating systems define a name beginning with two *directory-separator* characters as a *root-name* that identifies network or other resource locations. Some operating systems define a single letter followed by a colon as a drive specifier – a *root-name* identifying a specific device such as a disk drive. — *end note*]

**5** If a *root-name* is otherwise ambiguous, the possibility with the longest sequence of characters is chosen. [*Note 3*: On a POSIX-like operating system, it is impossible to have a *root-name* and a *relative-path* without an intervening *root-directory* element. — *end note*]

**6** *Normalization* of a generic format pathname means:

1. If the path is empty, stop.
2. Replace each slash character in the *root-name* with a *preferred-separator*.
3. Replace each *directory-separator* with a *preferred-separator*. [*Note 4*: The generic pathname grammar ([[fs.path.generic]](fs.path.generic.md)) defines *directory-separator* as one or more slashes and *preferred-separator*s. — *end note*]
4. Remove each dot filename and any immediately following *directory-separator*.
5. As long as any appear, remove a non-dot-dot filename immediately followed by a *directory-separator* and a dot-dot filename, along with any immediately following *directory-separator*.
6. If there is a *root-directory*, remove all dot-dot filenames and any *directory-separator*s immediately following them. [*Note 5*: These dot-dot filenames attempt to refer to nonexistent parent directories. — *end note*]
7. If the last filename is dot-dot, remove any trailing *directory-separator*.
8. If the path is empty, add a dot.

The result of normalization is a path in *normal form*, which is said to be *normalized*.
