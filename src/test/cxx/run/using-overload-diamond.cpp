// [namespace.udecl]/1, /9: repeated imports name the same declarations.
namespace source {
int pick(float) { return 1; }
template <class T> int pick(T*) { return 2; }
}

namespace relay {
int pick(char) { return 4; }
using source::pick;
}

namespace target {
int pick(int) { return 3; }
using source::pick;
using relay::pick;
}

int main() {
    int value = 0;
    return target::pick(0.0f) != 1 || target::pick(&value) != 2 ||
           target::pick(0) != 3 || target::pick(char{}) != 4;
}
