.text
pblendvb %xmm1, %xmm2
pblendvb %xmm0, %xmm1, %xmm2
pcmpestrm $0x4b, %xmm1, %xmm2
pcmpestri $0x4b, %xmm1, %xmm2
pcmpistrm $0x4b, %xmm1, %xmm2
pcmpistri $0x4b, %xmm1, %xmm2
pblendvb %xmm10, %xmm15
pblendvb %xmm0, %xmm10, %xmm15
pcmpestrm $0x4b, %xmm10, %xmm15
pcmpestri $0x4b, %xmm10, %xmm15
pcmpistrm $0x4b, %xmm10, %xmm15
pcmpistri $0x4b, %xmm10, %xmm15
pblendvb 24(%r12,%r13,4), %xmm11
pblendvb %xmm0, 24(%r12,%r13,4), %xmm11
pcmpestrm $0x4b, 24(%r12,%r13,4), %xmm11
pcmpestri $0x4b, 24(%r12,%r13,4), %xmm11
pcmpistrm $0x4b, 24(%r12,%r13,4), %xmm11
pcmpistri $0x4b, 24(%r12,%r13,4), %xmm11
pblendvb value(%rip), %xmm9
pblendvb %xmm0, value(%rip), %xmm9
pcmpestrm $0x4b, value(%rip), %xmm9
pcmpestri $0x4b, value(%rip), %xmm9
pcmpistrm $0x4b, value(%rip), %xmm9
pcmpistri $0x4b, value(%rip), %xmm9
iretq
uiret
clui
stui
.data
value: .zero 16
