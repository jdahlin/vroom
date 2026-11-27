/* [class.base.init]/7: a base with no constructors initialized by a
   parenthesized braced-init-list. */
extern "C" {
struct error_manager {
    void (*exit)(int);
    int code;
    int count;
};
}
struct empty_braces : error_manager {
    int *stack[4] = {};
    empty_braces() : error_manager({}) {}
};
struct filled : error_manager {
    filled() : error_manager({nullptr, 3, 4}) {}
};
int main()
{
    empty_braces e;
    filled f;
    return e.code + e.count + (e.stack[3] != nullptr) + f.code + f.count - 7;
}
