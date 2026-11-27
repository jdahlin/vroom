/* [over.match.ref]: explicit conversion functions are no candidates when a
   reference binds a class's converted result (Skia's raw_ptr pushed to a
   priority_queue<T *>). */
template <class T> struct rp { T *p; explicit operator bool() const { return p; } operator T *() const { return p; } template <class U> explicit operator U *() const { return static_cast<U *>(p); } };
struct Q { int r = 0; void push(int *const &v) { r += *v; } void push(int *&&v) { r += *v * 10; } };
int main()
{
    int x = 1;
    rp<int> a{&x};
    const rp<int> &c = a;
    Q q;
    q.push(c);
    q.push(a);
    return q.r - 20;
}
