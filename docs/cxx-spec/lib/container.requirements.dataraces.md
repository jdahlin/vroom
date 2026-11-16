# 22.2.2 Container data races [container.requirements.dataraces]

[Contents](../README.md) · up: [22.2 Container requirements \[container.requirements\]](container.requirements.md) · prev: [22.2.1 General container requirements \[container.requirements.general\]](container.requirements.general.md) · next: [22.2.3 Sequence containers \[sequence.reqmts\]](sequence.reqmts.md)

**1** For purposes of avoiding data races ([[res.on.data.races]](res.on.data.races.md)), implementations shall consider the following functions to be `const`: `begin`, `end`, `rbegin`, `rend`, `front`, `back`, `data`, `find`, `lower_bound`, `upper_bound`, `equal_range`, `at` and, except in associative or unordered associative containers, `operator[]`.

**2** Notwithstanding [[res.on.data.races]](res.on.data.races.md), implementations are required to avoid data races when the contents of the contained object in different elements in the same container, excepting `vector<bool>`, are modified concurrently.

**3** [*Note 1*: For a `vector<int> x` with a size greater than one, `x[1] = 5` and `*x.begin() = 10` can be executed concurrently without a data race, but `x[0] = 5` and `*x.begin() = 10` executed concurrently may result in a data race. As an exception to the general rule, for a `vector<bool> y`, `y[0] = true` may race with `y[1] = true`. — *end note*]
