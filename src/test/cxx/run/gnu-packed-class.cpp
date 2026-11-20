// [ext:gnu_attributes] packed and aligned on a class, in the class-head and
// after the closing brace (glibc's struct epoll_event, struct sockaddr)
extern "C" int printf(const char *, ...);

union data {
    void *ptr;
    long number;
};
struct __attribute__((packed)) head {
    char c;
    int i;
    long l;
};
struct tail {
    char c;
    data d;
    int i;
} __attribute__((__packed__));
struct both {
    char c;
    int i;
} __attribute__((packed, aligned(8)));
struct wide {
    int i;
} __attribute__((aligned(32)));
struct __attribute__((may_alias)) alias {
    int i;
};
struct nonstring {
    char text[4] __attribute__((nonstring));
};
struct holder {
    char c;
    tail t;
    head h;
};

int main()
{
    head h = {1, 2, 3};
    tail t = {1, {nullptr}, 4};
    both b = {1, 2};
    wide w = {1};
    alias a = {5};
    printf("%d %d %d %d %d\n", (int)sizeof(head), (int)alignof(head),
           (int)sizeof(tail), (int)alignof(tail), (int)sizeof(both));
    printf("%d %d %d\n", (int)alignof(both), (int)sizeof(wide),
           (int)alignof(wide));
    printf("%d %d %d\n", (int)sizeof(holder), (int)alignof(holder),
           (int)__builtin_offsetof(holder, h));
    return h.l + t.i + b.i + w.i + a.i == 15 ? 0 : 1;
}
