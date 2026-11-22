// [basic.scope.enum]/1, [dcl.enum]/5, [expr.prim.id.qual]/4
enum class FilterMode { Nearest, Linear, Last = Linear };
static constexpr int filter_mode_count = static_cast<int>(FilterMode::Last) + 1;

enum { Linear = 7, Offset = 2 };
enum class MipmapMode : unsigned char {
    Nearest,
    Linear,
    Last = Linear,
    WithOffset = Last + Offset,
};

int main() {
    return filter_mode_count != 2 ||
           MipmapMode::Last != MipmapMode::Linear ||
           static_cast<int>(MipmapMode::WithOffset) != 3 ||
           Linear != 7;
}
