int *bad(void) {
    int n = 3;
    return &n;
}
int main(void) { return *bad(); }
