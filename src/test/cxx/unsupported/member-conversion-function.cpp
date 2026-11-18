/* A conversion function: no rules yet ([class.conv.fct]); the note names
   the check in vcc that refused it. */
struct S {
    operator int() const { return 1; }
};
int i = S();
/* libc++'s spelling: attributes and constexpr before it */
struct L {
    __attribute__((__visibility__("hidden"))) constexpr operator bool() const noexcept { return true; }
};
