# Both fields need relocations. The RIP-relative addend includes the trailing immediate.
.text
movw $target+7, target+12(%rip)
movw $target+1, target-8(%rbx,%r9,4)
# Numeric RIP displacement stays literal even when followed by an immediate.
movl $0x12345678, 32(%rip)
