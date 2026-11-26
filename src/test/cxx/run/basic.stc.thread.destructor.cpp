// [basic.stc.thread]: block-scope thread_local variable with user destructor.

static void (*registered_dtor)(void *) = nullptr;
static void *registered_obj = nullptr;
static int destroyed = 0;

extern "C" int __cxa_thread_atexit(void (*dtor)(void *), void *obj, void *)
{
    registered_dtor = dtor;
    registered_obj = obj;
    return 0;
}

struct Box {
    int value;
    Box(int v) : value(v) {}
    ~Box() { destroyed += value; }
};

static Box &get_box()
{
    thread_local static Box b(42);
    return b;
}

int main()
{
    if (get_box().value != 42)
        return 1;
    if (!registered_dtor || registered_obj != &get_box())
        return 2;
    get_box().value = 99;
    if (get_box().value != 99)
        return 3;
    registered_dtor(registered_obj);
    if (destroyed != 99)
        return 4;
    return 0;
}
