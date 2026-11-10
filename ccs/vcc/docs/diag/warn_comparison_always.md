# A comparison of a variable with itself

`x == x`, `x <= x` and `x >= x` are always true and `x != x`, `x < x` and `x > x` always
false (for integers; for floating point `x != x` is the test for NaN, and vcc does not warn
there).  The comparison is almost always a typo for another variable.

<!-- flags: -Wtautological-compare -->

```c wrong
int same(int a, int b) {
	return a == a;
}
```

```c fixed
int same(int a, int b) {
	return a == b;
}
```
