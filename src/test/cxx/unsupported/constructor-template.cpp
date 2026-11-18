// [temp.mem]/1: a construction of a class with a constructor template,
// which overload resolution would consider ([over.match.ctor])
struct Zero {
    template <class T> consteval Zero(T) {}
};
Zero z = 0;
