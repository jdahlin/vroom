// [dcl.init]/8: value-initialization in new storage zeroes it there, with no
// temporary of its size on the stack.
void *operator new(decltype(sizeof 0), void *p) noexcept { return p; }
struct Big { short w[8000000]; int n; };
alignas(Big) static unsigned char raw[sizeof(Big)];
int main() {
    raw[0] = 1;
    Big *b = new (raw) Big();
    return b->n + b->w[0];
}
