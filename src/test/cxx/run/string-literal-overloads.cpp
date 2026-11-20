/* [diff.lex]/3: a string literal converts to char* (clang's extension, a
   warning), but a function that needs the conversion is worse than any
   viable function that does not: a const char* overload (glibc's
   strchr(char *, int) / strchr(const char *, int) pair), void*, bool, an
   ellipsis, a user-defined conversion, whatever the other arguments */
extern "C++" {
char *pick(char *s, int) { return s; }
const char *pick(const char *s, int) { return s + 1; }
}
int which(char *) { return 1; }
int which(const void *) { return 2; }
int flag(char *) { return 1; }
int flag(bool) { return 2; }
int ell(char *) { return 1; }
int ell(...) { return 2; }
int mixed(char *, int) { return 1; }
int mixed(const char *, unsigned) { return 2; }
int two(char *, int) { return 1; }
int two(char *, long) { return 2; }
struct text {
    text(const char *) {}
};
int user(char *) { return 1; }
int user(text) { return 2; }
int only(char *s) { return s[0]; }
int dbl(char *) { return 1; }
int dbl(double) { return 2; }
int main()
{
    int bad = 0;
    if (*pick("ab", 0) != 'b')
        bad |= 1;
    if (which("ab") != 2)
        bad |= 2;
    if (flag("ab") != 2)
        bad |= 4;
    char buffer[3] = {'x', 'y', 0};
    if (*pick(buffer, 0) != 'x')
        bad |= 8;
    if (only("ab") != 'a')
        bad |= 16;
    if (ell("ab") != 2)
        bad |= 32;
    if (mixed("ab", 0) != 2)
        bad |= 64;
    if (two("ab", 0) != 1)
        bad |= 128;
    if (user("ab") != 2)
        bad |= 256;
    if (dbl("ab") != 1)
        bad |= 512;
    return bad != 0;
}
