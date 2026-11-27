// [dcl.attr.unused]: [[maybe_unused]] before an explicit specialization
// changes nothing.
template <class T> int get(T v) { return v; }
template <> [[maybe_unused]] inline int get(long v) { return int(v) + 40; }

int main() { return get(2L) + get(1) - 43; }
