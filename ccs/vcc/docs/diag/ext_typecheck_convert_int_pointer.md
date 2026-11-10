# Integer converted to a pointer

An integer is used where a pointer is expected (an initializer, an assignment, an
argument, a return value).  C allows the conversion only with a cast; clang and vcc make
it an error by default (`-Wint-conversion`), because it is nearly always a missing `&` or a
call to a function that was not declared.

When the integer is an object, vcc suggests taking its address; when it is the result of
a call, check that the function is declared with the right return type.

```c wrong
void set(int *p) { *p = 1; }
void run(void) {
	int value = 0;
	int *ptr = value;
	set(ptr);
}
```

```c fixed
void set(int *p) { *p = 1; }
void run(void) {
	int value = 0;
	int *ptr = &value;
	set(ptr);
}
```
