// namespace-scope constants' linkage: const (arrays too) is internal unless declared extern, here or before;
// extern with an initializer is a definition
extern const int e1[2];
const int e1[2] = {1, 2};
extern const int e2;
const int e2 = 3;
extern const int e3 = 4;
const int i1[] = {5, 6};
const char s1[] = "ab";
const int *p(int k) { return k == 1 ? e1 : k == 2 ? &e2 : k == 3 ? &e3 : i1; }
int main() { return p(1)[1] == 2 && *p(2) == 3 && *p(3) == 4 && p(4)[1] == 6 && s1[1] == 'b' ? 0 : 1; }
