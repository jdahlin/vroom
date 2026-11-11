movl x(% rip), %eax
ud1 %eax,%ecx
ud1l 8(%rax),%ecx
ud1 (%edx), %rdi
ud1q (%rax),%rdi
x:
