lea (%ecx,%ecx,2), %eax
movw (%eax),%bx
lock incl (%eax)
vmovdqa (%eax),%ymm1
movl %fs:(%eax),%ebx
movl %ds:(%esi),%eax
movl 8(,%ecx,4),%eax
movl (%r8d),%eax
