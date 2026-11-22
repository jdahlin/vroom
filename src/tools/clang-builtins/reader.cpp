/* Parser and evaluator for the builtin tables' TableGen subset. */
#include "reader.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <utility>

namespace clang_builtins
{
static std::vector<Token> tokenize(const std::string &source,
                                   const fs::path &path)
{
    static const std::regex pattern(
        R"td(\s+|//[^\n]*|/\*[\s\S]*?\*/|\[\{[\s\S]*?\}\]|"(?:\\[^\n]|[^"\\\n])*"|[a-zA-Z_][a-zA-Z_0-9]*|-?(?:0[xX][0-9a-fA-F]+|[0-9]+)|[<>{}=:;,.#?!$()\[\]])td");
    std::vector<Token> tokens;
    unsigned line = 1;
    for (auto pos = source.cbegin(); pos != source.cend();) {
        std::match_results<std::string::const_iterator> match;
        if (!std::regex_search(pos, source.cend(), match, pattern,
                               std::regex_constants::match_continuous))
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line) +
                ": unsupported token starting with " + *pos);
        auto word = match.str();
        if (!std::isspace(static_cast<unsigned char>(word.front())) &&
            !word.starts_with("//") && !word.starts_with("/*"))
            tokens.push_back({word, line});
        for (char c : word)
            line += c == '\n';
        pos = match[0].second;
    }
    tokens.push_back({"", line});
    return tokens;
}

class Parser
{
    Database &db;
    fs::path path;
    std::vector<Token> tokens;
    size_t position = 0;
    Fields bindings;
    std::string name_prefix;
    unsigned anonymous = 0;

    std::string location() const
    {
        return path.string() + ":" + std::to_string(tokens[position].line);
    }

    [[noreturn]] void fail(const std::string &message) const
    {
        throw std::runtime_error(location() + ": " + message);
    }

    const std::string &peek() const
    {
        return tokens[position].text;
    }

    bool take(std::string_view expected)
    {
        if (peek() != expected)
            return false;
        ++position;
        return true;
    }

    void expect(std::string_view expected)
    {
        if (!take(expected))
            fail("expected '" + std::string(expected) + "', got '" + peek() +
                 "'");
    }

    std::string identifier()
    {
        if (peek().empty() ||
            !(std::isalpha(static_cast<unsigned char>(peek()[0])) ||
              peek()[0] == '_'))
            fail("expected identifier");
        return tokens[position++].text;
    }

    std::string string_literal()
    {
        if (!peek().starts_with('"'))
            fail("expected string");
        std::string result;
        while (peek().starts_with('"')) {
            const auto &value = tokens[position++].text;
            for (size_t i = 1; i + 1 < value.size(); ++i) {
                char c = value[i];
                if (c == '\\') {
                    c = value[++i];
                    switch (c) {
                    case 'n':
                        c = '\n';
                        break;
                    case 't':
                        c = '\t';
                        break;
                    case '"':
                    case '\\':
                        break;
                    default:
                        fail("unsupported string escape");
                    }
                }
                result += c;
            }
        }
        return result;
    }

    std::vector<Expr> values(std::string_view end)
    {
        std::vector<Expr> result;
        if (!take(end)) {
            do {
                result.push_back(value());
            } while (take(",") && peek() != end);
            expect(end);
        }
        return result;
    }

    Expr atom()
    {
        if (take("!")) {
            auto op = identifier();
            if (take("<")) {
                type();
                expect(">");
            }
            expect("(");
            if (op == "cond") {
                std::vector<Expr> args;
                do {
                    args.push_back(value());
                    expect(":");
                    args.push_back(value());
                } while (take(","));
                expect(")");
                return {Expr::operation, std::move(op), std::move(args)};
            }
            return {Expr::operation, std::move(op), values(")")};
        }
        if (take("(")) {
            // DAG bodies are used by the NEON header emitter. The spelling
            // extractor only needs to know whether the operation has a body.
            unsigned depth = 1;
            while (depth && !peek().empty()) {
                if (take("("))
                    ++depth;
                else if (take(")"))
                    --depth;
                else
                    ++position;
            }
            if (depth)
                fail("unterminated dag");
            return {Expr::dag, {}, {}};
        }
        if (take("?"))
            return {};
        if (take("["))
            return {Expr::list, {}, values("]")};
        if (peek().starts_with('"'))
            return {Expr::literal, string_literal(), {}};
        if (peek().starts_with("[{") || peek().starts_with('-') ||
            (!peek().empty() &&
             std::isdigit(static_cast<unsigned char>(peek()[0]))))
            return {Expr::literal, tokens[position++].text, {}};
        auto name = identifier();
        if (take("<"))
            return {Expr::specialization, std::move(name), values(">")};
        return {Expr::reference, std::move(name), {}};
    }

    Expr primary()
    {
        auto result = atom();
        while (take("."))
            result = {Expr::member, identifier(), {std::move(result)}};
        return result;
    }

    Expr value()
    {
        auto result = primary();
        while (take("#"))
            result = {Expr::paste, {}, {std::move(result), primary()}};
        return result;
    }

    void type()
    {
        identifier();
        if (take("<")) {
            type();
            expect(">");
        }
    }

    Assignment field(bool override)
    {
        if (!override)
            type();
        auto name = identifier();
        Expr initializer;
        if (override || peek() == "=") {
            expect("=");
            initializer = value();
        }
        return {std::move(name), std::move(initializer)};
    }

    std::vector<Assignment> parameters()
    {
        std::vector<Assignment> result;
        if (take("<")) {
            do {
                result.push_back(field(false));
            } while (take(","));
            expect(">");
        }
        return result;
    }

    std::vector<Base> bases()
    {
        std::vector<Base> result;
        if (take(":")) {
            do {
                auto name = identifier();
                auto args = take("<") ? values(">") : std::vector<Expr>{};
                result.push_back({std::move(name), std::move(args)});
            } while (take(","));
        }
        return result;
    }

    Value resolve(const Expr &expr) const
    {
        Record context;
        context.location = location();
        std::set<std::string> active;
        return db.evaluate(expr, bindings, context, active);
    }

    std::string record_name()
    {
        std::string result;
        bool explicit_prefix = false;
        if (peek() != ":" && peek() != ";" && peek() != "{") {
            do {
                if (peek() == "!") {
                    auto part = resolve(atom());
                    if (part.kind != Expr::literal)
                        fail("expected literal in record name");
                    result += part.text;
                } else if (peek().starts_with('"')) {
                    result += string_literal();
                } else if (!peek().empty() &&
                           std::isdigit(
                               static_cast<unsigned char>(peek().front()))) {
                    result += tokens[position++].text;
                } else {
                    auto token = identifier();
                    explicit_prefix |= token == "NAME";
                    auto it = bindings.find(token);
                    result += it == bindings.end() ? token : it->second.text;
                }
            } while (take("#"));
        }
        if (result.empty())
            result = "$" + std::to_string(db.definitions.size()) + "_" +
                     std::to_string(anonymous++);
        return explicit_prefix ? result : name_prefix + result;
    }

    void block(const std::vector<Assignment> &scoped)
    {
        if (take("{")) {
            while (!take("}"))
                object(scoped);
        } else {
            object(scoped);
        }
    }

    void object(const std::vector<Assignment> &scoped)
    {
        if (take("include")) {
            auto include = fs::path(string_literal());
            db.read((include.has_parent_path() ? db.include_root
                                               : path.parent_path()) /
                    include);
            return;
        }
        if (take("let")) {
            auto overrides = scoped;
            do {
                overrides.push_back(field(true));
            } while (take(","));
            expect("in");
            block(overrides);
            return;
        }
        if (take("defvar")) {
            auto assignment = field(true);
            auto value = resolve(assignment.value);
            expect(";");
            db.globals.insert_or_assign(assignment.name, std::move(value));
            return;
        }
        if (take("foreach")) {
            auto loop = field(true);
            auto items = resolve(loop.value);
            if (items.kind == Expr::literal && peek().starts_with('-')) {
                int first = std::stoi(items.text);
                int last = -std::stoi(tokens[position++].text);
                items = {Expr::list, {}, {}};
                for (long long i = first; i <= last; ++i)
                    items.elements.push_back(
                        {Expr::literal, std::to_string(i), {}});
            }
            if (items.kind != Expr::list || items.elements.empty())
                fail("expected a nonempty foreach list");
            expect("in");
            auto start = position;
            auto outer = bindings;
            for (const auto &item : items.elements) {
                position = start;
                bindings.insert_or_assign(loop.name, item);
                block(scoped);
                bindings = outer;
            }
            return;
        }
        if (take("multiclass")) {
            auto name = identifier();
            auto params = parameters();
            expect("{");
            auto start = position;
            unsigned depth = 1;
            while (depth && !peek().empty()) {
                if (take("{"))
                    ++depth;
                else if (take("}"))
                    --depth;
                else
                    ++position;
            }
            if (depth)
                fail("unterminated multiclass");
            std::vector<Token> body(tokens.begin() + start,
                                    tokens.begin() + position - 1);
            body.push_back({"", tokens[position - 1].line});
            if (!db.multiclasses
                     .try_emplace(name, Multiclass{path, std::move(body),
                                                   std::move(params)})
                     .second)
                fail("duplicate multiclass " + name);
            return;
        }
        if (take("defm")) {
            auto name = record_name();
            auto parents = bases();
            expect(";");
            for (const auto &parent : parents) {
                auto it = db.multiclasses.find(parent.name);
                if (it == db.multiclasses.end())
                    fail("unknown multiclass " + parent.name);
                if (!db.active_multiclasses.insert(parent.name).second)
                    fail("recursive multiclass " + parent.name);
                const auto &[origin, body, params] = it->second;
                if (parent.args.size() > params.size())
                    fail("too many multiclass arguments");
                Parser parser(db, origin, body, name);
                parser.bindings = bindings;
                parser.bindings.insert_or_assign(
                    "NAME", Value{Expr::literal, name, {}});
                for (size_t i = 0; i < params.size(); ++i) {
                    const auto &[key, expr] = params[i];
                    auto arg = i < parent.args.size() ? resolve(parent.args[i])
                                                      : parser.resolve(expr);
                    if (arg.kind == Expr::unset)
                        fail("missing multiclass argument " + key);
                    parser.bindings.insert_or_assign(key, std::move(arg));
                }
                parser.parse(scoped);
                db.active_multiclasses.erase(parent.name);
            }
            return;
        }

        auto loc = location();
        bool is_class = take("class");
        if (!is_class)
            expect("def");
        Record record;
        record.location = std::move(loc);
        record.name = is_class ? identifier() : record_name();
        record.bindings = bindings;
        record.overrides = scoped;
        if (is_class)
            record.parameters = parameters();
        record.bases = bases();
        if (!take(";")) {
            expect("{");
            while (!take("}")) {
                record.fields.push_back(field(take("let")));
                expect(";");
            }
        }
        auto &records = is_class ? db.classes : db.definitions;
        if (!records.try_emplace(record.name, std::move(record)).second)
            fail("duplicate record");
    }

  public:
    Parser(Database &db, const fs::path &path, const std::string &source)
        : db(db), path(path), tokens(tokenize(source, path))
    {
    }

    Parser(Database &db, const fs::path &path, const std::vector<Token> &body,
           std::string prefix)
        : db(db), path(path), tokens(body), name_prefix(std::move(prefix))
    {
    }

    void parse(const std::vector<Assignment> &scoped = {})
    {
        while (!peek().empty())
            object(scoped);
    }
};

void Database::read(const fs::path &path)
{
    if (!active_files.insert(path).second)
        throw std::runtime_error(path.string() + ": recursive include");
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error(path.string() + ": cannot open input");
    std::string source{std::istreambuf_iterator<char>(file), {}};
    Parser(*this, path, source).parse();
    active_files.erase(path);
}

Value Database::evaluate(const Expr &expr, const Fields &fields,
                         const Record &record,
                         std::set<std::string> &active) const
{
    switch (expr.kind) {
    case Expr::unset:
    case Expr::literal:
    case Expr::dag:
        return {expr.kind, expr.text, {}};
    case Expr::reference:
        if (auto it = fields.find(expr.text); it != fields.end())
            return it->second;
        if (auto it = globals.find(expr.text); it != globals.end())
            return it->second;
        if (expr.text == "true" || expr.text == "false")
            return {Expr::literal, expr.text == "true" ? "1" : "0", {}};
        if (definitions.contains(expr.text))
            return {Expr::reference, expr.text, {}};
        break;
    case Expr::specialization: {
        auto parent = classes.find(expr.text);
        if (parent == classes.end())
            break;
        std::vector<Value> args;
        for (const auto &arg : expr.args)
            args.push_back(evaluate(arg, fields, record, active));
        instantiate(parent->second, args, active);
        return {Expr::specialization, expr.text, std::move(args)};
    }
    case Expr::list: {
        Value result{Expr::list, {}, {}};
        for (const auto &arg : expr.args)
            result.elements.push_back(evaluate(arg, fields, record, active));
        return result;
    }
    case Expr::paste: {
        auto lhs = evaluate(expr.args[0], fields, record, active);
        const auto &right = expr.args[1];
        auto rhs = right.kind == Expr::reference &&
                           !fields.contains(right.text) &&
                           !globals.contains(right.text)
                       ? Value{Expr::literal, right.text, {}}
                       : evaluate(right, fields, record, active);
        if (lhs.kind == Expr::literal && rhs.kind == Expr::literal)
            return {Expr::literal, lhs.text + rhs.text, {}};
        break;
    }
    case Expr::operation: {
        if (expr.text == "cond" && expr.args.size() % 2 == 0) {
            for (size_t i = 0; i < expr.args.size(); i += 2) {
                if (evaluate(expr.args[i], fields, record, active).text == "1")
                    return evaluate(expr.args[i + 1], fields, record, active);
            }
            break;
        }
        std::vector<Value> args;
        for (const auto &arg : expr.args)
            args.push_back(evaluate(arg, fields, record, active));
        if (expr.text == "eq" && args.size() == 2)
            return {
                Expr::literal, args[0].text == args[1].text ? "1" : "0", {}};
        if (expr.text == "toupper" && args.size() == 1) {
            for (char &c : args[0].text)
                c = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
            return args[0];
        }
        if (expr.text == "listconcat") {
            Value result{Expr::list, {}, {}};
            for (const auto &arg : args) {
                if (arg.kind != Expr::list)
                    throw std::runtime_error(record.location +
                                             ": expected list in !listconcat");
                result.elements.insert(result.elements.end(),
                                       arg.elements.begin(),
                                       arg.elements.end());
            }
            return result;
        }
        break;
    }
    case Expr::member: {
        auto base = evaluate(expr.args[0], fields, record, active);
        if (base.kind != Expr::reference)
            break;
        auto instance = instantiate(definitions.at(base.text), {}, active);
        if (auto it = instance.fields.find(expr.text);
            it != instance.fields.end())
            return it->second;
        break;
    }
    }
    throw std::runtime_error(record.location + ": unresolved value " +
                             expr.text);
}

Instance Database::instantiate(const Record &record,
                               const std::vector<Value> &args,
                               std::set<std::string> &active,
                               std::string_view name) const
{
    if (!active.insert(record.name).second)
        throw std::runtime_error(record.location + ": recursive record " +
                                 record.name);
    if (args.size() > record.parameters.size())
        throw std::runtime_error(record.location +
                                 ": too many template arguments");
    Fields locals = record.bindings;
    if (name.empty())
        name = record.name;
    locals.insert_or_assign("NAME",
                            Value{Expr::literal, std::string(name), {}});
    for (size_t i = 0; i < record.parameters.size(); ++i) {
        const auto &[name, expr] = record.parameters[i];
        auto value =
            i < args.size() ? args[i] : evaluate(expr, locals, record, active);
        if (value.kind == Expr::unset)
            throw std::runtime_error(record.location +
                                     ": missing template argument " + name);
        locals.emplace(name, std::move(value));
    }
    Instance result;
    for (const auto &base : record.bases) {
        auto parent = classes.find(base.name);
        if (parent == classes.end())
            throw std::runtime_error(record.location + ": unknown base " +
                                     base.name);
        std::vector<Value> values;
        for (const auto &arg : base.args)
            values.push_back(evaluate(arg, locals, record, active));
        auto inherited = instantiate(parent->second, values, active, name);
        result.classes.merge(inherited.classes);
        result.classes.insert(base.name);
        for (auto &[name, value] : inherited.fields)
            result.fields.insert_or_assign(name, std::move(value));
    }
    for (const auto *group : {&record.overrides, &record.fields}) {
        for (const auto &[name, expr] : *group) {
            Fields scope = result.fields;
            for (const auto &[key, value] : locals)
                scope.insert_or_assign(key, value);
            result.fields.insert_or_assign(
                name, evaluate(expr, scope, record, active));
        }
    }
    active.erase(record.name);
    return result;
}

} // namespace clang_builtins
