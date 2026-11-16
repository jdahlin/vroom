# 29.2.1 Imbue limitations [iostream.limits.imbue]

[Contents](../README.md) · up: [29.2 Iostreams requirements \[iostreams.requirements\]](iostreams.requirements.md) · prev: [29.2 Iostreams requirements \[iostreams.requirements\]](iostreams.requirements.md) · next: [29.2.2 Positioning type limitations \[iostreams.limits.pos\]](iostreams.limits.pos.md)

**1** No function described in [[input.output]](input.output.md) except for `ios_base::imbue` and `basic_filebuf::pubimbue` causes any instance of `basic_ios::imbue` or `basic_streambuf::imbue` to be called. If any user function called from a function declared in [[input.output]](input.output.md) or as an overriding virtual function of any class declared in [[input.output]](input.output.md) calls `imbue`, the behavior is undefined.
