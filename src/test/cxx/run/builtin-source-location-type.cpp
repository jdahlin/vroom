/* [ext:clang_source_location] the type of __builtin_source_location():
   pointer to const std::source_location::__impl, which libc++'s
   <source_location> takes with decltype. */
namespace std {
struct source_location {
    struct __impl {
        const char *_M_file_name;
        const char *_M_function_name;
        unsigned _M_line;
        unsigned _M_column;
    };
};
} // namespace std

using location_pointer = decltype(__builtin_source_location());
static_assert(__is_same(location_pointer, const std::source_location::__impl *),
              "");

int main()
{
    location_pointer p = nullptr;
    return p ? 1 : 0;
}
