    .text
    .globl variadic_observer
    .type  variadic_observer, @function

    .extern g_variadic_entry_snapshot
    .extern variadic_observer_impl

variadic_observer:
    # Save entry registers into global snapshot WITHOUT clobbering arg regs.
    # We must preserve %al (in %rax) for variadic FP arguments.

    lea g_variadic_entry_snapshot(%rip), %r10

    mov %rdi, 0x00(%r10)
    mov %rsi, 0x08(%r10)
    mov %rdx, 0x10(%r10)
    mov %rcx, 0x18(%r10)
    mov %r8,  0x20(%r10)
    mov %r9,  0x28(%r10)

    mov %rsp, 0x30(%r10)
    mov %rbp, 0x38(%r10)

    mov %rax, 0x40(%r10)   # includes %al

    # Tail jump: keep original stack/return address/alignment
    jmp variadic_observer_impl

    .size variadic_observer, .-variadic_observer
