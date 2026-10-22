/* One serialized operation and game-thread RPC gateway. Native read semantics
 * belong to the controller; this blob only preserves/publishes the C ABI. */
.text
.global bw_path_hook_start,bw_path_hook_end,bw_path_slot,bw_path_kind
.global bw_path_failure_return,bw_path_errno_fn
.type bw_path_hook_start,@function
bw_path_hook_start:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    /* Entry RSP=8 mod16. Six pushes +72 local bytes align every C call. */
    sub $72,%rsp
    mov %rdi,0(%rsp)
    mov %rsi,8(%rsp)
    mov %rdx,16(%rsp)
    mov %rcx,24(%rsp)
    mov %r8,32(%rsp)
    mov %r9,40(%rsp)
    .byte 0x48,0xbb
bw_path_slot:
    .quad 0
    .byte 0xbd
bw_path_kind:
    .long 0
    .byte 0x49,0xbc
bw_path_failure_return:
    .quad 0
    .byte 0x49,0xbd
bw_path_errno_fn:
    .quad 0
    call .Lpath_deadline
.Lpath_acquire:
    cmpl $0,28(%rbx)
    jne .Lpath_fail
    xor %eax,%eax
    mov $3,%edx
    lock cmpxchg %edx,(%rbx)
    je .Lpath_publish
    pause
    lfence
    rdtsc
    shl $32,%rdx
    or %rdx,%rax
    cmp 48(%rsp),%rax
    jb .Lpath_acquire
    jmp .Lpath_timeout
.Lpath_publish:
    cmpl $0,28(%rbx)
    jne .Lpath_fail
    mov %ebp,4(%rbx)
    mov 0(%rsp),%rax
    mov %rax,128(%rbx)
    mov %rax,8(%rbx)
    mov 8(%rsp),%rax
    mov %rax,136(%rbx)
    mov %rax,16(%rbx)
    mov 16(%rsp),%rax
    mov %rax,144(%rbx)
    mov %rax,32(%rbx)
    mov 24(%rsp),%rax
    mov %rax,152(%rbx)
    mov %rax,48(%rbx)
    mov 32(%rsp),%rax
    mov %rax,160(%rbx)
    mov 40(%rsp),%rax
    mov %rax,168(%rbx)
    movl $0,24(%rbx)
    movq $0,120(%rbx)
    movl $1,(%rbx)
.Lpath_wait:
    cmpl $0,28(%rbx)
    jne .Lpath_fail
    cmpl $2,(%rbx)
    je .Lpath_complete
    cmpl $6,(%rbx)
    je .Lpath_call
    pause
    lfence
    rdtsc
    shl $32,%rdx
    or %rdx,%rax
    cmp 48(%rsp),%rax
    jb .Lpath_wait
.Lpath_timeout:
    lock orl $1,28(%rbx)
    jmp .Lpath_fail
.Lpath_call:
    mov 56(%rbx),%rax
    mov 64(%rbx),%rdi
    mov 72(%rbx),%rsi
    mov 80(%rbx),%rdx
    mov 88(%rbx),%rcx
    mov 96(%rbx),%r8
    mov 104(%rbx),%r9
    call *%rax
    mov %rax,112(%rbx)
    call .Lpath_deadline
    movl $7,(%rbx)
    jmp .Lpath_wait
.Lpath_complete:
    /* Claim the completed operation before releasing ownership; a poisoned
     * late acknowledgement must not be consumed as successful native data. */
    mov $2,%eax
    mov $3,%edx
    lock cmpxchg %edx,(%rbx)
    jne .Lpath_wait
    cmpl $0,28(%rbx)
    jne .Lpath_fail
    mov 120(%rbx),%rax
    mov %rax,56(%rsp)
    movl $0,(%rbx)
    cmpl $0,28(%rbx)
    jne .Lpath_fail
    mov 56(%rsp),%rax
    jmp .Lpath_return
.Lpath_fail:
    lock orl $1,28(%rbx)
    test %r13,%r13
    je .Lpath_failure_value
    call *%r13
    test %rax,%rax
    je .Lpath_failure_value
    movl $5,(%rax)
.Lpath_failure_value:
    mov %r12,%rax
.Lpath_return:
    add $72,%rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret
.Lpath_deadline:
    lfence
    rdtsc
    shl $32,%rdx
    or %rdx,%rax
    add 40(%rbx),%rax
    mov %rax,56(%rsp)
    ret
bw_path_hook_end:
.size bw_path_hook_start,bw_path_hook_end-bw_path_hook_start
.section .note.GNU-stack,"",@progbits
