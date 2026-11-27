#ifndef VCC_PORT_THREAD_H
#define VCC_PORT_THREAD_H
/*
 * port::thread: std::thread's interface over pthreads, with no C++ library
 * (vcc is built with -nostdinc++ and links none).  As std::thread: the
 * callable and its arguments are moved or copied into the new thread; a
 * joinable thread that is destroyed or assigned to aborts.  Not provided:
 * pointers to members as the callable, get_id.  Without exceptions, a
 * thread that cannot be started aborts.
 *
 * Compile jobs belong on the pool (port/task.h), which keeps them inside
 * the CPU budget; this is for threads with their own life.
 */
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

namespace port
{

namespace detail
{
template <class T> struct decay {
    using type = __decay(T);
};
/* our own placement new: <new> is the C++ library's */
struct place {
    void *at;
};
} // namespace detail

} // namespace port

inline void *operator new(size_t, port::detail::place p) noexcept
{
    return p.at;
}
inline void operator delete(void *, port::detail::place) noexcept
{
}

namespace port
{

class thread
{
  public:
    using native_handle_type = pthread_t;

    thread() noexcept = default;

    template <class F, class... A>
        requires(!__is_same(typename detail::decay<F>::type, thread))
    explicit thread(F &&f, A &&...a)
    {
        start(
            [f = static_cast<F &&>(f), ... a = static_cast<A &&>(a)]() mutable {
                f(static_cast<decltype(a) &&>(a)...);
            });
    }

    thread(thread &&o) noexcept : handle(o.handle), running(o.running)
    {
        o.running = false;
    }
    thread &operator=(thread &&o) noexcept
    {
        if (running)
            abort();
        handle = o.handle;
        running = o.running;
        o.running = false;
        return *this;
    }
    thread(const thread &) = delete;
    thread &operator=(const thread &) = delete;
    ~thread()
    {
        if (running)
            abort();
    }

    bool joinable() const noexcept
    {
        return running;
    }
    native_handle_type native_handle() const noexcept
    {
        return handle;
    }
    void join()
    {
        if (!running || pthread_join(handle, nullptr))
            abort();
        running = false;
    }
    void detach()
    {
        if (!running || pthread_detach(handle))
            abort();
        running = false;
    }
    static unsigned hardware_concurrency() noexcept
    {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n < 0)
            return 0;
        return static_cast<unsigned>(n);
    }

  private:
    template <class C> static void *run(void *p)
    {
        C *c = static_cast<C *>(p);
        (*c)();
        c->~C();
        free(c);
        return nullptr;
    }
    template <class C> void start(C &&c)
    {
        static_assert(alignof(C) <= alignof(max_align_t),
                      "port::thread: over-aligned capture");
        void *p = malloc(sizeof(C));
        if (!p)
            abort();
        new (detail::place{p}) C(static_cast<C &&>(c));
        if (pthread_create(&handle, nullptr, run<C>, p))
            abort();
        running = true;
    }

    pthread_t handle{};
    bool running = false;
};

} // namespace port

#endif
