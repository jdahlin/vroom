#include <string>
#include <vector>
#include <map>
#include <memory>
#include <utility>
void s1(std::string) { }
void s2(const std::string &, std::string &&, std::string *) { }
void s3(std::vector<int>, std::vector<std::string> &, const std::vector<std::vector<int>> &) { }
void s4(std::map<std::string, int> &, std::pair<int, long>, std::pair<std::string, std::string> *) { }
void s5(std::unique_ptr<int> &, std::shared_ptr<std::string>, std::allocator<char>) { }
void s6(std::string, std::string, std::vector<std::string>, std::allocator<std::string>) { }
void s7(std::vector<int>::iterator, std::string::const_iterator, std::map<int, int>::iterator) { }
void s8(std::char_traits<char>, std::less<int>, std::initializer_list<int>) { }
namespace app { struct Cfg { }; void run(const std::vector<Cfg> &, std::map<std::string, Cfg> *, std::pair<Cfg, Cfg>) { } }
