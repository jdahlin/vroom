// a specialization named through a typedef before its template is defined is completed by the use
// that needs it (libc++'s std::streampos is fpos<mbstate_t> named in <iosfwd>, fpos defined later;
// basic_streambuf's inline bodies do streampos(streamoff(-1)))
template <class T> struct F;
typedef F<int> FI;
template <class T> struct F { T v; F(long x = 0) : v((T)x) {} };
template <class C> struct Buf { FI seek(long o) { return FI(o); } };
int main() {
  Buf<char> b;
  return sizeof(FI) == sizeof(int) && b.seek(-1).v == -1 ? 0 : 1;
}
