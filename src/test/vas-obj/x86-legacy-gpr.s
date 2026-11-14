# Every legacy GPR spelling, including high bytes and the low bytes that need REX.
.text
movq %RAX, %rcx
movq %RDX, %rbx
movq %RSP, %rbp
movq %RSI, %rdi
movl %EAX, %ecx
movl %EDX, %ebx
movl %ESP, %ebp
movl %ESI, %edi
movw %AX, %cx
movw %DX, %bx
movw %SP, %bp
movw %SI, %di
movb %AL, %cl
movb %DL, %bl
movb %SPL, %bpl
movb %SIL, %dil
movb %AH, %ch
movb %DH, %bh
