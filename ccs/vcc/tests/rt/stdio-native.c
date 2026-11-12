/* Native file-I/O ABI: freopen returns a full-width FILE pointer; fscanf
   reads through that returned stream. Bare-metal RT has no file-I/O backend. */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char *name = tmpnam(0);
    if (!name) abort();
    FILE *f = fopen(name, "w");
    if (!f) abort();
    if (fprintf(f, "%d %s\n", 12345, "roundtrip") < 0) abort();
    FILE *r = freopen(name, "r", f);
    if (!r || r != f) abort();
    int n = 0;
    char text[16] = {0};
    if (fscanf(r, "%d %15s", &n, text) != 2) abort();
    if (n != 12345 || text[0] != 'r' || text[8] != 'p' || text[9]) abort();
    if (fclose(r) || remove(name)) abort();
    return 0;
}
