# An assignment used as a condition

`if (x = 1)` assigns 1 to `x` and then tests the result, which is always true.  It is
usually a typo for the comparison `x == 1`.  When the assignment is intended (`while ((c =
next()))`), an extra pair of parentheses says so and silences the warning.

vcc offers both fixes as notes, the comparison and the parentheses; neither is applied by
`--fix`, since only the author knows which one is meant.

```c wrong
int is_one(int x) {
	if (x = 1)
		return 1;
	return 0;
}
```

```c fixed
int is_one(int x) {
	if (x == 1)
		return 1;
	return 0;
}
```
