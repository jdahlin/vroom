// [dcl.attr.grammar]: an attribute without effect after a declarator.
const int &pass(const int &a [[clang::lifetimebound]]) { return a; }

int main() {
    int n = 5;
    return pass(n) != 5;
}
