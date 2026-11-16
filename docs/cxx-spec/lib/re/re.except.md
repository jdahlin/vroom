# 30.11.1 Exceptions [re.except]

[Contents](../../README.md) · up: [30.11 Regular expression algorithms \[re.alg\]](re.alg.md) · prev: [30.11 Regular expression algorithms \[re.alg\]](re.alg.md) · next: [30.11.2 `regex_match` \[re.alg.match\]](re.alg.match.md)

**1** The algorithms described in subclause [[re.alg]](re.alg.md) may throw an exception of type `regex_error`. If such an exception `e` is thrown, `e.code()` shall return either `regex_constants::error_complexity` or `regex_constants::error_stack`.
