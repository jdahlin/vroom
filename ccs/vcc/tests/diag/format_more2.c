/* -Wformat: format_arg (gettext), MS I/I32/I64 and w length modifiers, *n$ widths and precisions */
int printf(const char *, ...);
char *gettext(const char *) __attribute__((format_arg(1)));
char *mygt(int, const char *) __attribute__((format_arg(2)));
void f(long l, int w){
  printf(gettext("%d\n"), l);
  printf(mygt(1, "%s"), l);
  printf("%I64d %w32d\n", l, l);
  printf("%1$*2$d\n", l, w);
  printf("%1$*2$d\n", w, l);
}
void f3(long l, int w){
  printf("%1$*2$d\n", l, w);
  printf("%1$*2$d\n", w, l);
  printf("%1$.*2$f %1$*2$.*3$f\n", 1.0, l, w);
  printf("%1$*3$d\n", w, w);
  printf("%*d\n", l, w);
}
void f4(long l, int i){ printf("%wd|%w32d|%Id|%I32d|%I64x|%I6d|%Iz", i, i, i, i, i, i, i);}
