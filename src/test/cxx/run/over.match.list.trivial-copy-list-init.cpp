// [over.match.list]/1: list-initializing a non-aggregate trivially copyable
// class from a single lvalue of the same class (like libc++'s
// weekday_indexed::__wd_{__wdval}).
class Day {
    unsigned char d;
public:
    Day() = default;
    constexpr explicit Day(unsigned v) : d(static_cast<unsigned char>(v)) {}
    constexpr unsigned value() const { return d; }
};

class IndexedDay {
    Day day;
    unsigned char idx;
public:
    constexpr IndexedDay(const Day &d, unsigned i)
        : day{d}, idx(static_cast<unsigned char>(i))
    {
    }
    constexpr unsigned total() const { return day.value() + idx; }
};

int main()
{
    unsigned v = 5;
    Day d(v);
    IndexedDay id(d, 3);
    return id.total() == 8 ? 0 : 1;
}
