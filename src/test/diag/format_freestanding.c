/* -Wformat: only library builtins declared with their type; none under -ffreestanding */
/* diag-flags: -ffreestanding */
int printf(const char *, ...);
void f(long l, char *s){ printf("%2$d %1$s\n", s, l); printf("%d\n", l);}
