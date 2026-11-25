// [over.match.copy], [over.match.best]/2.4: a converting constructor and a converting constructor
// template's specialization with conversions alike: the one that is no template's specialization
// converts (json's result = value_t::discarded: basic_json(value_t) over
// basic_json(CompatibleType&&))
enum class kind { a, b, discarded };
struct js {
  int how;
  js(const kind) : how(1) {}
  template <class T> js(T &&) : how(2) {}
  js(const js &o) : how(o.how) {}
  js &operator=(js o) {
    how = o.how;
    return *this;
  }
};
int main() {
  js j(2.0);
  int first = j.how;
  j = kind::discarded;
  return first == 2 && j.how == 1 ? 0 : 1;
}
