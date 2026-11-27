// [class.mem]/6: a constructor body names members of a nested class declared
// after it.
struct O {
    int m;
    O() {
        using R = Config::Renderability;
        R r = Config::kYes;
        m = r + Config::kYes + (int)Config::Renderability::kNo;
    }
    struct Config {
        enum Renderability { kNo, kYes };
    };
};
int main() { O o; return o.m - 2; }
