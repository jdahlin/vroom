__thread int t = 3; __thread int tb; static __thread int st; extern __thread int et;
__attribute__((visibility("hidden"))) __thread int ht;
int get(void) { return t + tb + st + et + ht; }
int *addr(void) { return &et; }
void set(int v) { t = v; st = v; }
