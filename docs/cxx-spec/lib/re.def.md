# 30.2 Definitions [re.def]

[Contents](../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.1 General \[re.general\]](re.general.md) · next: [30.3 Requirements \[re.req\]](re.req.md)

**1** The following definitions shall apply to this Clause:

<a id="defns.regex.collating.element"></a>
### 30.2.1 collating element [defns.regex.collating.element]

a sequence of one or more characters within the current locale that collate as if they were a single character.

<a id="defns.regex.finite.state.machine"></a>
### 30.2.2 finite state machine [defns.regex.finite.state.machine]

an unspecified data structure that is used to represent a regular expression, and which permits efficient matches against the regular expression to be obtained.

<a id="defns.regex.format.specifier"></a>
### 30.2.3 format specifier [defns.regex.format.specifier]

a sequence of one or more characters that is to be replaced with some part of a regular expression match.

<a id="defns.regex.matched"></a>
### 30.2.4 matched [defns.regex.matched]

a sequence of zero or more characters is matched by a regular expression when the characters in the sequence correspond to a sequence of characters defined by the pattern.

<a id="defns.regex.primary.equivalence.class"></a>
### 30.2.5 primary equivalence class [defns.regex.primary.equivalence.class]

a set of one or more characters which share the same primary sort key: that is the sort key weighting that depends only upon character shape, and not accents, case, or locale specific tailorings.

<a id="defns.regex.regular.expression"></a>
### 30.2.6 regular expression [defns.regex.regular.expression]

a pattern that selects specific strings from a set of character strings.

<a id="defns.regex.subexpression"></a>
### 30.2.7 sub-expression [defns.regex.subexpression]

a subset of a regular expression that has been marked by parenthesis.
