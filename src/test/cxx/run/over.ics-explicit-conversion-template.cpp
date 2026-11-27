/* [over.match.ref], [over.match.copy]/1.2: an explicit conversion function
   template is no candidate when a class converts to a pointer parameter
   (Skia's raw_ptr). */
template <class T> struct rp { T *p; operator T *() const { return p; } template <class U> explicit operator U *() const { return static_cast<U *>(p); } };
struct Q { int r = 0; void push(int *const &v) { r += *v; } void push(int *&&v) { r += *v * 10; } };
int main(){ int x = 1; rp<int> a{&x}; const rp<int> &c = a; Q q; q.push(c); q.push(a); return q.r - 20; }
