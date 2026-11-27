/* [dcl.fct.default] in a generic lambda's parameters
   (GrGeometryProcessor's liftTransforms). */
extern "C" int printf(const char *, ...);

enum class Coord { local = 3, device = 4 };

int main()
{
    int total = 0;
    auto walk = [&, index = 0](auto &self, int depth, const char *p = nullptr,
                               int last = -1,
                               Coord c = Coord::local) mutable -> void {
        total = total * 10 + static_cast<int>(c) + last + (p != nullptr) + index++;
        if (depth)
            self(self, depth - 1);
    };
    walk(walk, 2);
    walk(walk, 0, "x", 5, Coord::device);
    auto add = [](auto a, int b = 7) { return a + b; };
    printf("%d %d %.1f %d\n", total, add(1), add(0.5), add(1L, 2) == 3);
    return 0;
}
