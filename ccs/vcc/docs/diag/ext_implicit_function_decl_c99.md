# Call to an undeclared function

A function is called before any declaration of it.  C89 assumed such a function returns
`int` and takes whatever arguments it is given; C99 removed that rule, and clang and vcc
treat it as an error.  The guess was dangerous: a function that returns a pointer or
takes a `double` is called with the wrong convention, and the program fails at run time.

Declare the function before the call: include the header that declares it (for a C
library function vcc names the header and offers the `#include` as a fix-it), or write a
prototype, or move the definition above its first use.

```c wrong
int main(void) {
	return answer();
}
int answer(void) { return 42; }
```

```c fixed
int answer(void) { return 42; }
int main(void) {
	return answer();
}
```
