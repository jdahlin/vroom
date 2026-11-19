// NULL (__null): the constant 0 of type long, a null pointer constant
#include <stddef.h>
char *p() { return NULL; }
int g(char *q) { return q == NULL; }
int h(long v) { return (int)v + 1; }
int main() { char *q = NULL; return g(p()) && g(q) && h(NULL) == 1 && sizeof(NULL) == sizeof(long) ? 0 : 1; }
