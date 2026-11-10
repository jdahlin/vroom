/* explorer stub: declarations only (there is no libc in the page) */
#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct _IO_FILE FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vprintf(const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
int puts(const char *);
int putchar(int);
int getchar(void);
int fputs(const char *, FILE *);
int fputc(int, FILE *);
int fgetc(FILE *);
char *fgets(char *, int, FILE *);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int scanf(const char *, ...);
int sscanf(const char *, const char *, ...);
#endif
