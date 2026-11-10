# A `printf` argument of the wrong type

The conversion specification says what type the next argument has (`%d`: `int`, `%ld`:
`long`, `%zu`: `size_t`, `%s`: a string); the argument has another type.  `printf` reads
the argument as the specification says, so the output is wrong, or the program fails when
the sizes differ (a `long` read as `int`, a number read as a string).

Change the specification (the fix-it shows the one that reads the argument's type) or
convert the argument.  For the `<stdint.h>` types prefer the `<inttypes.h>` macros
(`PRId64`) or `%zu`/`%td`/`%jd`: `int64_t` is `long` on Linux and `long long` on Apple, so
`%ld` is right on one and wrong on the other.

<!-- flags: -Wformat -->

```c wrong
int printf(const char *, ...);
void show(long count) {
	printf("%d items\n", count);
}
```

```c fixed
int printf(const char *, ...);
void show(long count) {
	printf("%ld items\n", count);
}
```
