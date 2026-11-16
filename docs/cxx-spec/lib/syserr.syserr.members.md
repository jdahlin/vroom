# 19.5.7.2 Members [syserr.syserr.members]

[Contents](../README.md) · up: [19.5.7 Class `system_error` \[syserr.syserr\]](syserr.syserr.md) · prev: [19.5.7.1 Overview \[syserr.syserr.overview\]](syserr.syserr.overview.md) · next: [20 General utilities library \[utilities\]](utilities.md)

```cpp
system_error(error_code ec, const string& what_arg);
```

**1** *Postconditions:* `code() == ec` and<br>`string_view(what()).find(what_arg.c_str()) != string_view::npos`.

```cpp
system_error(error_code ec, const char* what_arg);
```

**2** *Postconditions:* `code() == ec` and `string_view(what()).find(what_arg) != string_view::npos`.

```cpp
system_error(error_code ec);
```

**3** *Postconditions:* `code() == ec`.

```cpp
system_error(int ev, const error_category& ecat, const string& what_arg);
```

**4** *Postconditions:* `code() == error_code(ev, ecat)` and`string_view(what()).find(what_arg.c_str()) != string_view::npos`.

```cpp
system_error(int ev, const error_category& ecat, const char* what_arg);
```

**5** *Postconditions:* `code() == error_code(ev, ecat)` and`string_view(what()).find(what_arg) != string_view::npos`.

```cpp
system_error(int ev, const error_category& ecat);
```

**6** *Postconditions:* `code() == error_code(ev, ecat)`.

```cpp
const error_code& code() const noexcept;
```

**7** *Returns:* `ec` or `error_code(ev, ecat)`, from the constructor, as appropriate.

```cpp
const char* what() const noexcept override;
```

**8** *Returns:* An NTBS incorporating the arguments supplied in the constructor.

[*Note 1*: The returned NTBS might be the contents of `what_arg + ": " + code.message()`. — *end note*]
