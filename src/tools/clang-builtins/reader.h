#ifndef VCC_TOOLS_CLANG_BUILTINS_READER_H
#define VCC_TOOLS_CLANG_BUILTINS_READER_H

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace clang_builtins
{
namespace fs = std::filesystem;

struct Token {
    std::string text;
    unsigned line;
};

struct Expr {
    enum Kind {
        literal,
        reference,
        specialization,
        list,
        member,
        paste,
        operation,
        dag,
        unset
    } kind = unset;
    std::string text;
    std::vector<Expr> args;
};

struct Assignment {
    std::string name;
    Expr value;
};

struct Base {
    std::string name;
    std::vector<Expr> args;
};

struct Value {
    Expr::Kind kind = Expr::unset;
    std::string text;
    std::vector<Value> elements;
};

using Fields = std::map<std::string, Value>;

struct Record {
    std::string name;
    std::vector<Assignment> parameters;
    std::vector<Base> bases;
    std::vector<Assignment> fields;
    std::vector<Assignment> overrides;
    std::string location;
    Fields bindings;
};

struct Instance {
    Fields fields;
    std::set<std::string> classes;
};

struct Multiclass {
    fs::path path;
    std::vector<Token> body;
    std::vector<Assignment> parameters;
};

struct Database {
    std::map<std::string, Record> classes, definitions;
    std::map<std::string, Multiclass> multiclasses;
    Fields globals;
    std::set<fs::path> active_files;
    std::set<std::string> active_multiclasses;
    fs::path include_root;

    void read(const fs::path &path);

    Instance instantiate(const Record &record, const std::vector<Value> &args,
                         std::set<std::string> &active,
                         std::string_view name = {}) const;
    Value evaluate(const Expr &expr, const Fields &fields, const Record &record,
                   std::set<std::string> &active) const;
};

struct ArmVariant {
    std::string name;
    std::string_view result, operand;
    unsigned tuple_vectors = 0;
};

void extract_neon(const Record &record, const Fields &fields,
                  std::set<std::string> &names,
                  std::vector<ArmVariant> *variants);
void extract_scalable(const Fields &fields, bool sme,
                      std::set<std::string> &names,
                      std::vector<ArmVariant> *variants);
void extract_sve_reinterprets(std::set<std::string> &names,
                              std::vector<ArmVariant> *variants);

} // namespace clang_builtins

#endif
