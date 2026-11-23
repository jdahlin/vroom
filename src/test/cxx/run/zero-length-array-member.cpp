/* a member array of zero elements (clang's and GCC's zero-length array
   extension): no bytes, at its element type's alignment, anywhere in the
   class (glibc's struct file_handle ends with unsigned char f_handle[0]) */
struct file_handle {
    unsigned int handle_bytes;
    int handle_type;
    unsigned char f_handle[0];
};
struct middle {
    char c;
    long none[0];
    char d;
};
static_assert(sizeof(file_handle) == 8, "");
static_assert(sizeof(middle) == 16, "");
static_assert(__builtin_offsetof(middle, d) == 8, "");
int main()
{
    union {
        file_handle h;
        unsigned char bytes[12];
    } u = {};
    u.bytes[8] = 7;
    middle m = {};
    m.d = 3;
    return u.h.f_handle[0] + (int)((char *)&m.d - (char *)m.none) - 7 + m.d - 3;
}
