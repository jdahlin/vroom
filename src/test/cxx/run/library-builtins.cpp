// [ext:gnu_library_builtins] __builtin_ forms of C library functions,
// declared at their first use with C language linkage: calls of the
// library function
int check(const char *s) {
    char buffer[16];
    __builtin_memset(buffer, 0, sizeof buffer);
    __builtin_strcpy(buffer, s);
    if (__builtin_strlen(buffer) != 5 || __builtin_strcmp(buffer, "hello"))
        return 1;
    if (__builtin_memcmp(buffer, "help", 3) || !__builtin_strchr(buffer, 'l'))
        return 2;
    char copy[16];
    __builtin_memcpy(copy, buffer, 6);
    __builtin_snprintf(buffer, sizeof buffer, "%s-%d", copy, 7);
    if (__builtin_strncmp(buffer, "hello-7", 8) || __builtin_abs(-3) != 3)
        return 3;
    return 0;
}

int main() {
    if (check("hello"))
        __builtin_abort();
    __builtin_printf("%s %d\n", "ok", __builtin_ffs(8));
    return 0;
}
