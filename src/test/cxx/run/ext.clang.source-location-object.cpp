// [ext:clang_source_location] __builtin_source_location() in an evaluated
// context and as a default argument.
namespace std {
struct source_location {
    struct __impl {
        const char *_M_file_name;
        const char *_M_function_name;
        unsigned _M_line;
        unsigned _M_column;
    };
    const __impl *__ptr_ = nullptr;
    static constexpr source_location current(
        const __impl *__ptr = __builtin_source_location()) noexcept
    {
        source_location sl;
        sl.__ptr_ = __ptr;
        return sl;
    }
    constexpr unsigned line() const noexcept { return __ptr_ ? __ptr_->_M_line : 0; }
    constexpr const char *function_name() const noexcept
    {
        return __ptr_ ? __ptr_->_M_function_name : "";
    }
};
} // namespace std

static unsigned helper(std::source_location loc = std::source_location::current())
{
    return loc.line();
}

int main()
{
    auto a = std::source_location::current();
    unsigned b = helper();
    return (a.line() == 34 && b == 35) ? 0 : 1;
}
