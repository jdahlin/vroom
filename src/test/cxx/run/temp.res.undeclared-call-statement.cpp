/* [temp.res]: a statement calling a name declared nowhere in a template,
   found by argument-dependent lookup (fmt's flockfile). */
namespace n
{
struct file {
    int locked;
};
void lock_it(file *f) { f->locked++; }
} // namespace n
template <class F> auto flock(F *f) -> decltype(lock_it(f)) { lock_it(f); }

int main()
{
    n::file f = {0};
    flock(&f);
    return f.locked - 1;
}
