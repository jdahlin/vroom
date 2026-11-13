/* Windows x64 (UCRT): errno is per thread, behind _errno() */
int *_errno(void);
#define errno (*_errno())
#define EINVAL 22
#define ERANGE 34
#define ENOMEM 12
