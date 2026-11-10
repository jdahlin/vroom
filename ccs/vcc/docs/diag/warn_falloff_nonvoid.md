# The end of a non-void function is reachable

The function returns a value, but control can reach its closing `}` without a `return`.
The caller then reads an indeterminate value; calling such a function and using the
result is undefined behaviour.

Add the missing `return`, or, if the end really cannot be reached, call a `noreturn`
function there (`abort()`, `__builtin_unreachable()`).

<!-- flags: -Wreturn-type -->

```c wrong
int sign(int x) {
	if (x > 0)
		return 1;
	if (x < 0)
		return -1;
}
```

```c fixed
int sign(int x) {
	if (x > 0)
		return 1;
	if (x < 0)
		return -1;
	return 0;
}
```
