/*
 * probe_regs_sysv(RegSnapshot* out)
 *
 * Capture the incoming argument registers and stack pointer at the vary beginning
 * of the call. This must be done in a standalone assembly function so that the 
 * compiler doesn't overwrite registers with its own prologue before we can read them.
 *
 * SysV AMD64 calling convention (Linux):
 *   - 1st arg: RDI (here: out pointer)
 *   - Caller ensures 16-byte stack alignment before CALL.
 *
 * We store:
 *   - rdi..r9 at entry
 *   - rsp / rbp at entry
 *   - rax at entry (useful: %al for variadic calls)
 */

    .text
    .global probe_regs_sysv
    .type probe_regs_sysv, @function

probe_regs_sysv:
    /*
     * At entry:
     *  RDI = out
     * But we also want to store the *original* RDI value.
     * We'll use a scratch register (R10) to hold out pointer
     * while we store the incoming regs.
     */
    mov %rdi, %r10	/* r10 = out */

    /* Save incoming integer argument registers */
    mov %rsp, 0x30(%r10)	/* snapshot.rsp_at_entry */
    mov %rbp, 0x38(%r10)	/* snapshot.rbp_at_entry */

    /* Save rax at entry (variadic ABI uses %al) */
    mov %rax, 0x40(%r10)	/* snapshot.rax_at_entry */

    ret
   
    .size probe_regs_sysv, .-probe_regs_sysv	
