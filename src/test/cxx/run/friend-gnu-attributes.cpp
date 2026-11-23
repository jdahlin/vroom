// [dcl.attr.grammar]/5 is about the standard's attribute-specifier-seq: a
// friend declaration that is no definition may still have GCC's
// attributes ([ext:gnu_attributes]), as libc++'s noop_coroutine_handle.h
// writes _LIBCPP_HIDE_FROM_ABI friend ... noop_coroutine() noexcept;
struct handle {
    int value;
    __attribute__((__visibility__("hidden"))) friend handle make(int v);
};
handle make(int v) { return handle{v}; }
int main() { return make(4).value == 4 ? 0 : 1; }
