#ifndef _RT_STDIO_H
#define _RT_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct _rt_file FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define BUFSIZ 1024
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vprintf(const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
int vsprintf(char *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
int puts(const char *);
int putchar(int);
int fputs(const char *, FILE *);
int fputc(int, FILE *);
int putc(int, FILE *);
int getchar(void);
int fgetc(FILE *);
int getc(FILE *);
char *fgets(char *, int, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
size_t fread(void *, size_t, size_t, FILE *);
int fflush(FILE *);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
int feof(FILE *);
int sscanf(const char *, const char *, ...);
void perror(const char *);
int remove(const char *);
char *tmpnam(char *);
#endif
