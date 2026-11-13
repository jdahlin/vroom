/* -Wformat: Darwin %D %O %U */
/* diag-flags: --target=arm64-apple-macosx14 */
int printf(const char *, ...);
void f(long l, int i, char *s){ printf("%D %O %U %lD %hO %#D %+U %.2D|%D", i, l, s, i, i, i, i, i);}
void f2(void){ printf("%-D %+D % D %#D %0D %'D %.3D %-O %+O % O %#O %0O %'O %.3O %-U %+U % U %#U %0U %'U %.3U %LD %LO", 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1L,1L);}
