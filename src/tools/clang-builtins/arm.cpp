/* ACLE type variants and compiler builtin spellings for NEON, SVE and SME. */
#include "reader.h"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace clang_builtins
{
static std::vector<std::string_view> type_specs(std::string_view text)
{
    std::vector<std::string_view> result;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (std::islower(static_cast<unsigned char>(text[i]))) {
            result.push_back(text.substr(start, i + 1 - start));
            start = i + 1;
        }
    }
    if (start != text.size())
        throw std::runtime_error("incomplete Arm type specification");
    return result;
}

struct ArmType {
    std::string_view kind;
    unsigned bits;
};

static ArmType sve_type(std::string_view spec)
{
    std::string_view integer = spec.starts_with('U')   ? "u"
                               : spec.starts_with('P') ? "b"
                               : spec.starts_with('Q') ? "c"
                                                       : "s";
    switch (spec.back()) {
    case 'c':
        return {integer, 8};
    case 's':
        return {integer, 16};
    case 'i':
        return {integer, 32};
    case 'l':
        return {integer, 64};
    case 'q':
        return {integer, 128};
    case 'h':
        return {"f", 16};
    case 'f':
        return {"f", 32};
    case 'd':
        return {"f", 64};
    case 'b':
        return {"bf", 16};
    case 'm':
        return {"mf", 8};
    default:
        throw std::runtime_error("unknown SVE type " + std::string(spec));
    }
}

static ArmType sve_operand(ArmType type, char mod)
{
    switch (mod) {
    case 'd':
    case 'c':
    case 'p':
    case '{':
    case 's':
    case 'a':
        return type;
    case 'e':
        return {"u", type.bits / 2};
    case 'y':
        return {"s", type.bits / 2};
    case 'h':
    case 'R':
        return {type.kind, type.bits / 2};
    case 'q':
    case 'r':
        return {type.kind, type.bits / 4};
    case 'b':
    case '@':
        return {"u", type.bits / 4};
    case 'o':
        return {type.kind, type.bits * 4};
    case 'P':
        return {"b", 1};
    case 'K':
    case 'x':
        return {"s", type.bits};
    case 'L':
    case 'u':
        return {"u", type.bits};
    case 'i':
    case 'n':
    case 'f':
    case 'g':
        return {"u", 64};
    case 'k':
    case 't':
        return {"s", 32};
    case 'l':
    case '#':
        return {"s", 64};
    case 'm':
    case 'z':
        return {"u", 32};
    case 'w':
    case 'j':
        return {type.kind, 64};
    case '[':
        return {"u", 8};
    case 'O':
        return {"f", 16};
    case 'M':
        return {"f", 32};
    case 'N':
        return {"f", 64};
    case '$':
        return {"bf", 16};
    case '~':
    case '!':
        return {"mf", 8};
    default:
        throw std::runtime_error(std::string("unknown SVE name modifier ") +
                                 mod);
    }
}

static std::string sve_modifiers(std::string_view prototype)
{
    std::string mods;
    for (size_t i = 0; i < prototype.size(); ++i) {
        char mod = prototype[i];
        if (mod >= '2' && mod <= '4') {
            mod = 'd';
            if (i + 2 < prototype.size() && prototype[i + 1] == '.') {
                i += 2;
                mod = prototype[i];
            }
        }
        mods += mod;
    }
    return mods;
}

static void add_variant(std::set<std::string> &names,
                        std::vector<ArmVariant> *variants, std::string name,
                        std::string_view result, std::string_view operand = {},
                        unsigned tuple_vectors = 0)
{
    if (variants) {
        names.insert(name);
        variants->push_back({std::move(name), result, operand, tuple_vectors});
    } else {
        names.insert(std::move(name));
    }
}

void extract_scalable(const Fields &fields, bool sme,
                      std::set<std::string> &names,
                      std::vector<ArmVariant> *variants)
{
    auto prototype = sve_modifiers(fields.at("Prototype").text);
    const auto &types = fields.at("Types").text;
    auto pattern = fields.at("Name").text;
    std::erase(pattern, '[');
    std::erase(pattern, ']');
    for (const auto spec : type_specs(
             types.empty() ? std::string_view("i") : std::string_view(types))) {
        auto type = sve_type(spec);
        auto name = pattern;
        for (size_t pos = name.find('{'); pos != std::string::npos;
             pos = name.find('{', pos)) {
            if (pos + 2 >= name.size() || name[pos + 2] != '}')
                throw std::runtime_error("invalid SVE name template " + name);
            char slot = name[pos + 1];
            if (slot != 'd' && (slot < '0' || slot > '3'))
                throw std::runtime_error("invalid SVE name operand " + name);
            auto suffix = slot == 'd'
                              ? type
                              : sve_operand(type, prototype.at(slot - '0'));
            name.replace(
                pos, 3, std::string(suffix.kind) + std::to_string(suffix.bits));
        }
        name += fields.at("MergeSuffix").text;
        if (prototype.ends_with('>'))
            name += "_fpm";
        add_variant(names, variants,
                    (sme ? "__builtin_sme_" : "__builtin_sve_") + name,
                    types.empty() ? std::string_view{} : spec);
    }
}

void extract_sve_reinterprets(std::set<std::string> &names,
                              std::vector<ArmVariant> *variants)
{
    constexpr std::string_view types[] = {
        "s8",  "u8",  "mf8", "s16",  "u16", "s32", "u32",
        "s64", "u64", "f16", "bf16", "f32", "f64",
    };
    for (auto to : types)
        for (auto from : types)
            for (unsigned count = 1; count <= 4; ++count) {
                auto name = "__builtin_sve_reinterpret_" + std::string(to) +
                            "_" + std::string(from);
                if (count != 1)
                    name += "_x" + std::to_string(count);
                add_variant(names, variants, std::move(name), to, from, count);
            }
}

static ArmType neon_type(std::string_view spec)
{
    switch (spec.back()) {
    case 'k':
        return {"p", 128};
    case 'h':
        return {"f", 16};
    case 'f':
        return {"f", 32};
    case 'd':
        return {"f", 64};
    case 'b':
        return {"bf", 16};
    case 'm':
        return {"mf", 8};
    }
    auto width = std::string_view("csil").find(spec.back());
    if (width == std::string_view::npos)
        throw std::runtime_error("unknown NEON type " + std::string(spec));
    return {(spec.find('P') != std::string_view::npos)   ? "p"
            : (spec.find('U') != std::string_view::npos) ? "u"
                                                         : "s",
            8u << width};
}

static bool neon_has_scalar(std::string_view proto, std::string_view out,
                            std::string_view in)
{
    bool result = false;
    for (auto spec = out; !proto.empty(); spec = in) {
        auto length = proto.front() == '(' ? proto.find(')') + 1 : 1;
        if (!length)
            throw std::runtime_error("unterminated NEON prototype modifier");
        auto mods = proto.substr(0, length);
        if ((mods.find('V') != std::string_view::npos) ||
            (((mods.find('1') != std::string_view::npos) ||
              spec.ends_with("Pk")) &&
             mods.find_first_of("v*I234") == std::string_view::npos))
            result = true;
        proto.remove_prefix(length);
    }
    return result;
}

void extract_neon(const Record &record, const Fields &fields,
                  std::set<std::string> &names,
                  std::vector<ArmVariant> *variants)
{
    const auto &base_name = fields.at("Name").text;
    const auto &prototype = fields.at("Prototype").text;
    auto specs = type_specs(fields.at("Types").text);
    auto inputs = type_specs(fields.at("CartesianProductWith").text);
    const auto &kind = record.bases.front().name;
    for (auto out : specs) {
        auto from_types = inputs.empty() ? std::vector{out} : inputs;
        for (auto in : from_types) {
            bool quad = out.find_first_of("QH") != std::string_view::npos;
            bool in_quad = in.find_first_of("QH") != std::string_view::npos;
            if (!inputs.empty() && (out == in || quad != in_quad))
                continue;
            auto name = base_name;
            auto type = neon_type(out);
            if (name == "vcvt_f16_f32" || name == "vcvt_f32_f16" ||
                name == "vcvt_f32_f64" || name == "vcvt_f64_f32" ||
                name == "vcvt_f32_bf16") {
                add_variant(names, variants, "__builtin_neon_" + name, out, in);
                continue;
            }
            bool generic_vector = fields.at("TargetGuard").text == "neon" &&
                                  !neon_has_scalar(prototype, out, in);
            auto suffix = [&](ArmType t) -> std::string {
                if (generic_vector || kind == "VInst")
                    return {};
                if ((kind == "IInst" || kind == "IOpInst") &&
                    t.kind.size() == 1 && t.kind != "f")
                    t.kind = "i";
                return "_" + std::string(t.kind) + std::to_string(t.bits);
            };
            auto code = suffix(type);
            if (name.size() >= 3 && name.substr(name.size() - 3, 2) == "_x" &&
                std::isdigit(static_cast<unsigned char>(name.back())))
                name.insert(name.size() - 3, code);
            else
                name += code;
            auto input = neon_type(in);
            if (type.kind != input.kind || type.bits != input.bits ||
                quad != in_quad)
                name += suffix(input).empty() ? "_" : suffix(input);
            if (generic_vector)
                name += "_v";
            auto insert_suffix = [&](char c) {
                auto pos = name.find('_');
                name.insert(pos == std::string::npos ? name.size() : pos, 1, c);
            };
            if (quad && !(out.find('H') != std::string_view::npos))
                insert_suffix('q');
            if ((out.find('S') != std::string_view::npos)) {
                switch (type.bits) {
                case 8:
                    insert_suffix('b');
                    break;
                case 16:
                    insert_suffix('h');
                    break;
                case 32:
                    insert_suffix('s');
                    break;
                case 64:
                    insert_suffix('d');
                    break;
                default:
                    throw std::runtime_error("invalid NEON scalar size");
                }
            }
            add_variant(names, variants, "__builtin_neon_" + name, out, in);
        }
    }
}

} // namespace clang_builtins
