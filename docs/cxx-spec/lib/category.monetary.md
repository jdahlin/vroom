# 28.4.6 The monetary category [category.monetary]

[Contents](../README.md) · up: [28.4 Standard `locale` categories \[locale.categories\]](locale.categories.md) · prev: [28.4.5.4 Class template `time_put_byname` \[locale.time.put.byname\]](locale.time.put.byname.md) · next: [28.4.6.1 Class template `money_get` \[locale.money.get\]](locale.money.get.md)

**1** These templates handle monetary formats. A template parameter indicates whether local or international monetary formats are to be used.

**2** All specifications of member functions for `money_put` and `money_get` in the subclauses of [[category.monetary]](category.monetary.md) only apply to the specializations required in Tables [[tab:locale.category.facets]](locale.category.md#tab:locale.category.facets) and [[tab:locale.spec]](locale.category.md#tab:locale.spec) ([[locale.category]](locale.category.md)). Their members use their `ios_base&`, `ios_base::iostate&`, and `fill` arguments as described in [[locale.categories]](locale.categories.md), and the `moneypunct<>` and `ctype<>` facets, to determine formatting details.

## Subclauses

- [28.4.6.1 Class template `money_get` \[locale.money.get\]](locale.money.get.md)
  - [28.4.6.1.1 Members \[locale.money.get.members\]](locale.money.get.members.md)
  - [28.4.6.1.2 Virtual functions \[locale.money.get.virtuals\]](locale.money.get.virtuals.md)
- [28.4.6.2 Class template `money_put` \[locale.money.put\]](locale.money.put.md)
  - [28.4.6.2.1 Members \[locale.money.put.members\]](locale.money.put.members.md)
  - [28.4.6.2.2 Virtual functions \[locale.money.put.virtuals\]](locale.money.put.virtuals.md)
- [28.4.6.3 Class template `moneypunct` \[locale.moneypunct\]](locale.moneypunct.md)
  - [28.4.6.3.1 Members \[locale.moneypunct.members\]](locale.moneypunct.members.md)
  - [28.4.6.3.2 Virtual functions \[locale.moneypunct.virtuals\]](locale.moneypunct.virtuals.md)
- [28.4.6.4 Class template `moneypunct_byname` \[locale.moneypunct.byname\]](locale.moneypunct.byname.md)
