/* Offline spelling inventory for the pinned Clang builtin tables. */
#include "reader.h"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace clang_builtins
{
static bool neon_uses_builtin(const Database &db, const Fields &fields)
{
    const auto &operation = fields.at("Operation");
    std::set<std::string> active;
    const auto instance =
        operation.kind == Expr::specialization
            ? db.instantiate(db.classes.at(operation.text), operation.elements,
                             active)
            : db.instantiate(db.definitions.at(operation.text), {}, active);
    return instance.fields.at("Ops").elements.empty();
}

struct BuiltinMetadata {
    std::string spelling, record, prototype, substitution, features, header,
        languages, alias_of;
    std::string prototype_format = "c", operand_variant, arch_guard,
                sme_features;
    std::string acle_name, merge_suffix, intrinsic;
    unsigned tuple_vectors = 0;
    std::vector<std::string> attributes;
};

static std::string field_text(const Fields &fields, const char *name)
{
    auto it = fields.find(name);
    return it == fields.end() ? "" : it->second.text;
}

static void append_arm_metadata(std::vector<BuiltinMetadata> &metadata,
                                std::string_view entry, const Record *record,
                                const Fields &fields,
                                const std::vector<ArmVariant> &variants)
{
    BuiltinMetadata row;
    row.prototype_format =
        entry == "arm_sve.td" || entry == "arm_sme.td" ? "sve-sme" : "neon";
    row.header = std::string(entry.substr(0, entry.size() - 3)) + ".h";
    if (record) {
        row.record = record->name;
        row.prototype = field_text(fields, "Prototype");
        row.features = field_text(fields, "TargetGuard");
        if (row.prototype_format == "sve-sme")
            row.features = field_text(fields, "SVETargetGuard");
        row.sme_features = field_text(fields, "SMETargetGuard");
        row.arch_guard = field_text(fields, "ArchGuard");
        row.acle_name = field_text(fields, "Name");
        row.merge_suffix = field_text(fields, "MergeSuffix");
        row.intrinsic = field_text(fields, "LLVMIntrinsic");
        if (auto flags = fields.find("Flags"); flags != fields.end())
            for (const auto &flag : flags->second.elements)
                row.attributes.push_back(flag.text);
    } else {
        row.prototype_format = "sve-reinterpret";
        row.features = "sme|sve";
    }
    for (const auto &variant : variants) {
        row.spelling = variant.name;
        row.substitution = variant.result;
        row.operand_variant = variant.operand;
        row.tuple_vectors = variant.tuple_vectors;
        metadata.push_back(row);
    }
}

static std::set<std::string>
builtin_spellings(const Database &db, std::string_view entry,
                  std::vector<BuiltinMetadata> *metadata)
{
    std::set<std::string> names;
    bool neon = entry == "arm_neon.td" || entry == "arm_fp16.td";
    bool sve = entry == "arm_sve.td";
    bool sme = entry == "arm_sme.td";
    if (sve) {
        std::vector<ArmVariant> variants;
        extract_sve_reinterprets(names, metadata ? &variants : nullptr);
        if (metadata)
            append_arm_metadata(*metadata, entry, nullptr, {}, variants);
    }
    for (const auto &[name, record] : db.definitions) {
        std::set<std::string> active;
        auto [fields, classes] = db.instantiate(record, {}, active);
        if (classes.contains("Inst")) {
            try {
                std::vector<ArmVariant> variants;
                if (neon && neon_uses_builtin(db, fields))
                    extract_neon(record, fields, names,
                                 metadata ? &variants : nullptr);
                if (sve || sme)
                    extract_scalable(fields, sme, names,
                                     metadata ? &variants : nullptr);
                if (metadata && !variants.empty())
                    append_arm_metadata(*metadata, entry, &record, fields,
                                        variants);
            } catch (const std::exception &error) {
                throw std::runtime_error(record.location + ": " + name + ": " +
                                         error.what());
            }
        }
        if (!classes.contains("Builtin"))
            continue;
        const auto &spellings = fields.at("Spellings");
        if (spellings.kind != Expr::list)
            throw std::runtime_error(record.location +
                                     ": expected a list of spellings");
        std::vector<Value> affixes{{Expr::literal, "", {}}};
        std::vector<Value> substitutions = affixes;
        bool prefix = false;
        if (classes.contains("Template")) {
            affixes = fields.at("Affixes").elements;
            substitutions = fields.at("Substitutions").elements;
            if (affixes.empty() || affixes.size() != substitutions.size())
                throw std::runtime_error(record.location +
                                         ": invalid builtin template");
            prefix = fields.at("AsPrefix").text == "1";
        }
        std::string required_prefix;
        if (const auto &prefix_value = fields.at("RequiredNamePrefix");
            prefix_value.kind == Expr::reference)
            required_prefix =
                db.instantiate(db.definitions.at(prefix_value.text), {}, active)
                    .fields.at("Spelling")
                    .text;
        BuiltinMetadata row;
        if (metadata) {
            row.record = name;
            row.prototype = field_text(fields, "Prototype");
            row.features = field_text(fields, "Features");
            row.header = field_text(fields, "Header");
            row.languages = field_text(fields, "Languages");
            for (const auto &attr : fields.at("Attributes").elements)
                row.attributes.push_back(attr.text);
        }
        for (size_t i = 0; i < affixes.size(); ++i) {
            const auto &affix = affixes[i];
            row.substitution = substitutions[i].text;
            for (const auto &spelling : spellings.elements) {
                if (affix.kind != Expr::literal ||
                    spelling.kind != Expr::literal)
                    throw std::runtime_error(
                        record.location +
                        ": expected a string spelling or affix");
                auto full = prefix ? affix.text + spelling.text
                                   : spelling.text + affix.text;
                row.spelling = required_prefix + full;
                names.insert(row.spelling);
                if (metadata)
                    metadata->push_back(row);
                if (classes.contains("LibBuiltin") &&
                    fields.at("AddBuiltinPrefixedAlias").text == "1") {
                    auto alias = "__builtin_" + full;
                    names.insert(alias);
                    if (metadata) {
                        auto alias_row = row;
                        alias_row.spelling = std::move(alias);
                        alias_row.alias_of = row.spelling;
                        metadata->push_back(std::move(alias_row));
                    }
                }
            }
        }
    }
    return names;
}
static void json_string(std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::cout << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            std::cout << '\\' << c;
        else if (c < 32)
            std::cout << "\\u00" << hex[c >> 4] << hex[c & 15];
        else
            std::cout << c;
    }
    std::cout << '"';
}

static void write_metadata(const BuiltinMetadata &row)
{
    std::cout << '{';
    bool comma = false;
    const std::pair<std::string_view, std::string_view> fields[] = {
        {"spelling", row.spelling},
        {"record", row.record},
        {"prototype_template", row.prototype},
        {"type_substitution", row.substitution},
        {"features", row.features},
        {"header", row.header},
        {"languages", row.languages},
        {"alias_of", row.alias_of},
        {"prototype_format", row.prototype_format},
        {"operand_variant", row.operand_variant},
        {"arch_guard", row.arch_guard},
        {"sme_features", row.sme_features},
        {"acle_name_template", row.acle_name},
        {"merge_suffix", row.merge_suffix},
        {"intrinsic", row.intrinsic},
    };
    for (const auto &[key, value] : fields) {
        if (comma)
            std::cout << ',';
        json_string(key);
        std::cout << ':';
        json_string(value);
        comma = true;
    }
    std::cout << ",\"tuple_vectors\":" << row.tuple_vectors;
    std::cout << ",\"attributes\":[";
    comma = false;
    for (const auto &attr : row.attributes) {
        if (comma)
            std::cout << ',';
        json_string(attr);
        comma = true;
    }
    std::cout << "]}\n";
}

} // namespace clang_builtins

int main(int argc, char **argv)
{
    bool metadata = argc == 4 && std::string_view(argv[1]) == "--metadata";
    if (argc != 3 && !metadata) {
        std::fprintf(stderr, "usage: clang-builtin-reader [--metadata] "
                             "CLANG_INCLUDE ENTRY.td\n");
        return 2;
    }
    try {
        clang_builtins::Database db;
        db.include_root = argv[metadata ? 2 : 1];
        std::filesystem::path input = argv[metadata ? 3 : 2];
        db.read(input);
        std::vector<clang_builtins::BuiltinMetadata> rows;
        auto names = builtin_spellings(db, input.filename().string(),
                                       metadata ? &rows : nullptr);
        if (metadata) {
            if (rows.empty())
                throw std::runtime_error("table emitted no builtin metadata");
            for (const auto &row : rows)
                clang_builtins::write_metadata(row);
        } else {
            for (const auto &name : names)
                std::cout << name << '\n';
        }
        return std::cout ? 0 : 1;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
