/* -Wformat: printf family as library builtins (no attribute), %b/%B, %q, a const variable as the format */
int printf(const char *, ...);
void f(long l, char *s){
  static const char fmt[] = "%d %s\n";
  printf(fmt, l, s);
  const char g[] = "%d\n"; printf(g, s);
  printf("%b\n", s);
  printf("%qd\n", 1);
  printf("%2$d %1$s\n", s, l);
}
void f5(long l, unsigned u, char *s){ printf("%b %lb %B %#b\n", l, s, u, 1.0); printf("%qd %qu\n", l, s);}
void f6(void){ printf("%-b %+b % b %#b %0b %'b %.3b %-B %+B %#B %0B %'B %Lb %qb %Lx %qx %qn", 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,(long long*)0);}
