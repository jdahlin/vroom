# Use of an undeclared identifier

A name is used in an expression, but no declaration of it is visible at that point.  C
requires every variable, function and enumerator to be declared before its first use; a
declaration later in the file, or in a header that is not included, does not count.

The usual causes are a typo (vcc suggests the closest visible name when there is one), a
variable declared in an inner block and used after it ends, or a header that declares it
missing from the `#include` lines.

```c wrong
int total(int n) {
	for (int i = 0; i < n; i++) {
		int sum = 0;
		sum += i;
	}
	return sum;
}
```

```c fixed
int total(int n) {
	int sum = 0;
	for (int i = 0; i < n; i++)
		sum += i;
	return sum;
}
```
