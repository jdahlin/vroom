# An expected token is missing

The parser needed a particular token (a `;` after a declaration or statement, a `)` that
closes a `(`, a `}`) and found something else.  vcc points at the place where the token
belongs, which is usually the end of the previous line, not where the next token starts.

A missing `;` has a fix-it that `--fix` applies; for an unbalanced bracket the note shows
where the opening one is.

```c wrong
int f(void) {
	int x = 1
	return x;
}
```

```c fixed
int f(void) {
	int x = 1;
	return x;
}
```
