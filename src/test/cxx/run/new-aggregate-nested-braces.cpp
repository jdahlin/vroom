extern "C" int printf(const char*, ...);
enum class country { a, b };
struct age { int v; age(int r = 0) : v(r) {} };
struct name { const char* s; name(const char* r = "") : s(r) {} };
struct person { age m_age{}; name m_name{}; country c{}; };
int main() { person* p = new person{{42}, {"John"}, country::b}; printf("%d %s %d\n", p->m_age.v, p->m_name.s, (int)p->c); delete p; }
