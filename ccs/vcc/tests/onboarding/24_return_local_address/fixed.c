int *bad(void) {
    static int n = 3;
    return &n;
}
int main(void) { return *bad(); }
