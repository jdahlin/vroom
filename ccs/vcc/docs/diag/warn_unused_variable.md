# An unused variable

A local variable is declared but never used.  It is often left over from a change, or a
sign that the wrong variable is used somewhere else.  Remove it; if it must stay (it is
used only in a configuration that is compiled out), mark it `__attribute__((unused))` (C23: `[[maybe_unused]]`),
or cast it to `void`.

<!-- flags: -Wunused-variable -->

```c wrong
int area(int w, int h) {
	int perimeter = 2 * (w + h);
	return w * h;
}
```

```c fixed
int area(int w, int h) {
	return w * h;
}
```
