/*
 * diff.cpp: [diff] Compatibility: what changed between editions, so that the
 * front end can ask for a named change instead of comparing years.  The table
 * is language-features.inc.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

struct language_feature_description {
    const char *name;
    standard_edition since;
    standard_edition until; /* never: still in the language */
    const char *reference;
};

constexpr language_feature_description language_features[] = {
#define LANGUAGE_FEATURE(name, since, until, reference)                        \
    {#name, standard_edition::since, standard_edition::until, reference},
#include "language-features.inc"
#undef LANGUAGE_FEATURE
};

} // namespace

/* whether the edition in effect has the feature */
bool implementation_defined_behavior::has(language_feature feature) const
{
    const language_feature_description &f =
        language_features[static_cast<int>(feature)];
    return edition >= f.since && edition < f.until;
}

const char *language_feature_name(language_feature feature)
{
    return language_features[static_cast<int>(feature)].name;
}

const char *language_feature_reference(language_feature feature)
{
    return language_features[static_cast<int>(feature)].reference;
}

} // namespace cplusplus

namespace cplusplus
{

namespace
{

struct extension_description {
    const char *name;
    extension_vendor vendor;
    const char *reference;
};

constexpr extension_description extensions[] = {
#define EXTENSION(name, vendor, reference)                                     \
    {#name, extension_vendor::vendor, reference},
#include "../ext/extensions.inc"
#undef EXTENSION
};

} // namespace

bool implementation_defined_behavior::has(extension e) const
{
    unsigned vendor = (unsigned)extensions[static_cast<int>(e)].vendor;
    if (vendor == (unsigned)extension_vendor::microsoft)
        return microsoft_extensions;
    return (extension_vendors >> vendor & 1) &&
           !(extensions_off >> static_cast<int>(e) & 1);
}

const char *extension_name(extension e)
{
    return extensions[static_cast<int>(e)].name;
}

extension_vendor extension_vendor_of(extension e)
{
    return extensions[static_cast<int>(e)].vendor;
}

const char *extension_reference(extension e)
{
    return extensions[static_cast<int>(e)].reference;
}

} // namespace cplusplus
