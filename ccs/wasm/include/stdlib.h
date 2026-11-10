/* explorer stub: declarations only */
#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
_Noreturn void abort(void);
_Noreturn void exit(int);
int atoi(const char *);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
double strtod(const char *, char **);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
int abs(int);
long labs(long);
int rand(void);
void srand(unsigned);
char *getenv(const char *);
#endif
