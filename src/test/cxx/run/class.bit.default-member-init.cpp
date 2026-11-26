// [class.bit]/1: C++20 default member initializer on a bit-field
// (e.g. libc++ __format/unicode.h:53).

struct ConsumeResult {
    unsigned int code_point : 31 = 65;
    enum : unsigned int { ok = 0, err = 1 } status : 1 {ok};
};

static_assert(sizeof(ConsumeResult) == sizeof(unsigned int));

int main()
{
    ConsumeResult r;
    if (r.code_point != 65)
        return 1;
    if (r.status != ConsumeResult::ok)
        return 2;
    ConsumeResult r2{42, ConsumeResult::err};
    if (r2.code_point != 42 || r2.status != ConsumeResult::err)
        return 3;
    return 0;
}
