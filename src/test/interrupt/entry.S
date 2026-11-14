.text
.globl invoke0
.type invoke0,@function
invoke0:
 push %rbx
 push %rbp
 push %r12
 push %r13
 push %r14
 push %r15
 fxsave64 saved_fp(%rip)
 fninit
 fld1
 fld1
 fld1
 fld1
 fld1
 fld1
 fld1
 fld1
 pcmpeqd %xmm0,%xmm0
 pcmpeqd %xmm1,%xmm1
 pcmpeqd %xmm2,%xmm2
 pcmpeqd %xmm3,%xmm3
 pcmpeqd %xmm4,%xmm4
 pcmpeqd %xmm5,%xmm5
 pcmpeqd %xmm6,%xmm6
 pcmpeqd %xmm7,%xmm7
 pcmpeqd %xmm8,%xmm8
 pcmpeqd %xmm9,%xmm9
 pcmpeqd %xmm10,%xmm10
 pcmpeqd %xmm11,%xmm11
 pcmpeqd %xmm12,%xmm12
 pcmpeqd %xmm13,%xmm13
 pcmpeqd %xmm14,%xmm14
 pcmpeqd %xmm15,%xmm15
 fxsave64 expected_fp(%rip)
 mov %rsp,%r11
 xor %eax,%eax
 mov %ss,%ax
 push %rax
 push %r11
 std
 pushfq
 xor %eax,%eax
 mov %cs,%ax
 push %rax
 lea .Lresume0(%rip),%rax
 push %rax
 mov $100,%rax
 mov $101,%rcx
 mov $102,%rdx
 mov $103,%rbx
 mov $105,%rbp
 mov $106,%rsi
 mov $107,%rdi
 mov $108,%r8
 mov $109,%r9
 mov $110,%r10
 mov $111,%r11
 mov $112,%r12
 mov $113,%r13
 mov $114,%r14
 mov $115,%r15
 jmp handler0
.Lresume0:
 mov %rax,got+0(%rip)
 mov %rcx,got+8(%rip)
 mov %rdx,got+16(%rip)
 mov %rbx,got+24(%rip)
 mov %rbp,got+40(%rip)
 mov %rsi,got+48(%rip)
 mov %rdi,got+56(%rip)
 mov %r8,got+64(%rip)
 mov %r9,got+72(%rip)
 mov %r10,got+80(%rip)
 mov %r11,got+88(%rip)
 mov %r12,got+96(%rip)
 mov %r13,got+104(%rip)
 mov %r14,got+112(%rip)
 mov %r15,got+120(%rip)
 pushfq
 popq returned_flags(%rip)
 cld
 fxsave64 actual_fp(%rip)
 fxrstor64 saved_fp(%rip)
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %rbp
 pop %rbx
 ret
.size invoke0,.-invoke0
.globl invoke1
.type invoke1,@function
invoke1:
 push %rbx
 push %rbp
 push %r12
 push %r13
 push %r14
 push %r15
 fxsave64 saved_fp(%rip)
 fninit
 fld1
 fld1
 fld1
 fld1
 fld1
 fld1
 fld1
 fld1
 pcmpeqd %xmm0,%xmm0
 pcmpeqd %xmm1,%xmm1
 pcmpeqd %xmm2,%xmm2
 pcmpeqd %xmm3,%xmm3
 pcmpeqd %xmm4,%xmm4
 pcmpeqd %xmm5,%xmm5
 pcmpeqd %xmm6,%xmm6
 pcmpeqd %xmm7,%xmm7
 pcmpeqd %xmm8,%xmm8
 pcmpeqd %xmm9,%xmm9
 pcmpeqd %xmm10,%xmm10
 pcmpeqd %xmm11,%xmm11
 pcmpeqd %xmm12,%xmm12
 pcmpeqd %xmm13,%xmm13
 pcmpeqd %xmm14,%xmm14
 pcmpeqd %xmm15,%xmm15
 fxsave64 expected_fp(%rip)
 mov %rsp,%r11
 xor %eax,%eax
 mov %ss,%ax
 push %rax
 push %r11
 std
 pushfq
 xor %eax,%eax
 mov %cs,%ax
 push %rax
 lea .Lresume1(%rip),%rax
 push %rax
 push $0x12345678
 mov $100,%rax
 mov $101,%rcx
 mov $102,%rdx
 mov $103,%rbx
 mov $105,%rbp
 mov $106,%rsi
 mov $107,%rdi
 mov $108,%r8
 mov $109,%r9
 mov $110,%r10
 mov $111,%r11
 mov $112,%r12
 mov $113,%r13
 mov $114,%r14
 mov $115,%r15
 jmp handler1
.Lresume1:
 mov %rax,got+0(%rip)
 mov %rcx,got+8(%rip)
 mov %rdx,got+16(%rip)
 mov %rbx,got+24(%rip)
 mov %rbp,got+40(%rip)
 mov %rsi,got+48(%rip)
 mov %rdi,got+56(%rip)
 mov %r8,got+64(%rip)
 mov %r9,got+72(%rip)
 mov %r10,got+80(%rip)
 mov %r11,got+88(%rip)
 mov %r12,got+96(%rip)
 mov %r13,got+104(%rip)
 mov %r14,got+112(%rip)
 mov %r15,got+120(%rip)
 pushfq
 popq returned_flags(%rip)
 cld
 fxsave64 actual_fp(%rip)
 fxrstor64 saved_fp(%rip)
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %rbp
 pop %rbx
 ret
.size invoke1,.-invoke1
.section .note.GNU-stack,"",@progbits
