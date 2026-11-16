# 12.4.3.1.1 Standard conversion sequences [over.ics.scs]

[Contents](README.md) · up: [12.4.3.1 Implicit conversion sequences \[over.best.ics\]](over.best.ics.md) · prev: [12.4.3.1 Implicit conversion sequences \[over.best.ics\]](over.best.ics.md) · next: [12.4.3.1.2 User-defined conversion sequences \[over.ics.user\]](over.ics.user.md)

**1** [Table 16](over.ics.scs.md#tab:over.ics.scs) summarizes the conversions defined in [[conv]](conv.md) and partitions them into four disjoint categories: Lvalue Transformation, Qualification Adjustment, Promotion, and Conversion. [*Note 1*: These categories are orthogonal with respect to value category, cv-qualification, and data representation: the Lvalue Transformations do not change the cv-qualification or data representation of the type; the Qualification Adjustments do not change the value category or data representation of the type; and the Promotions and Conversions do not change the value category or cv-qualification of the type. — *end note*]

**2** [*Note 2*: As described in [[conv]](conv.md), a standard conversion sequence either is the Identity conversion by itself (that is, no conversion) or consists of one to three conversions from the other four categories. If there are two or more conversions in the sequence, the conversions are applied in the canonical order: **Lvalue Transformation**, **Promotion** or **Conversion**, **Qualification Adjustment**. — *end note*]

**3** Each conversion in [Table 16](over.ics.scs.md#tab:over.ics.scs) also has an associated rank (Exact Match, Promotion, or Conversion). These are used to rank standard conversion sequences ([[over.ics.rank]](over.ics.rank.md)). The rank of a conversion sequence is determined by considering the rank of each conversion in the sequence and the rank of any reference binding ([[over.ics.ref]](over.ics.ref.md)). If any of those has Conversion rank, the sequence has Conversion rank; otherwise, if any of those has Promotion rank, the sequence has Promotion rank; otherwise, the sequence has Exact Match rank.

<a id="tab:over.ics.scs"></a>
**Table 16: Conversions** [tab:over.ics.scs]

| Conversion | Category | Rank | Subclause |
| --- | --- | --- | --- |
| No conversions required | Identity | Exact Match |  |
| Lvalue-to-rvalue conversion | Lvalue Transformation | Exact Match | [[conv.lval]](conv.lval.md) |
| Array-to-pointer conversion | Lvalue Transformation | Exact Match | [[conv.array]](conv.array.md) |
| Function-to-pointer conversion | Lvalue Transformation | Exact Match | [[conv.func]](conv.func.md) |
| Qualification conversions | Qualification Adjustment | Exact Match | [[conv.qual]](conv.qual.md) |
| Function pointer conversion | Qualification Adjustment | Exact Match | [[conv.fctptr]](conv.fctptr.md) |
| Integral promotions | Promotion | Promotion | [[conv.prom]](conv.prom.md) |
| Floating-point promotion | Promotion | Promotion | [[conv.fpprom]](conv.fpprom.md) |
| Integral conversions | Conversion | Conversion | [[conv.integral]](conv.integral.md) |
| Floating-point conversions | Conversion | Conversion | [[conv.double]](conv.double.md) |
| Floating-integral conversions | Conversion | Conversion | [[conv.fpint]](conv.fpint.md) |
| Pointer conversions | Conversion | Conversion | [[conv.ptr]](conv.ptr.md) |
| Pointer-to-member conversions | Conversion | Conversion | [[conv.mem]](conv.mem.md) |
| Boolean conversions | Conversion | Conversion | [[conv.bool]](conv.bool.md) |
