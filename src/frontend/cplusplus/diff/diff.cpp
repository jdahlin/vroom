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
