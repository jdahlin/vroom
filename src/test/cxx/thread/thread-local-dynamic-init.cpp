// [stmt.dcl]/3: a block-scope thread_local with a constructor is made once in
// each thread that reaches it.
extern "C" {
typedef unsigned long pthread_t;
int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
}
int made;
struct B {
    int v;
    B() : v(7) { __atomic_add_fetch(&made, 1, __ATOMIC_SEQ_CST); }
};
int use() {
    static thread_local B b;
    return b.v++;
}
int other;
void *run(void *) {
    other = use();
    other = other * 10 + use();
    return nullptr;
}
int main() {
    if (use() != 7 || use() != 8)
        return 1;
    pthread_t t;
    if (pthread_create(&t, nullptr, run, nullptr) || pthread_join(t, nullptr))
        return 2;
    return other != 78 || made != 2 || use() != 9;
}
