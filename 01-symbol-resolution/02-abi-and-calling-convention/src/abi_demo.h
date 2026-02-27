#pragma once
#include <stdint.h>

#ifdef __x86_64__

/*
 * Snapshot of register state at the *very beginning* of a function,
 * before the compiler-generated prologue can clobber argument registers.
 *
 * SysV AMD64 integer/pointer args: RDI, RSI, RDX, RCX, R8, R9
 * Return value: RAX (integer), XMM0 (float/double)
 *
 * Note: This is not a full CPU context. It's only what we choose to capture.
 */
typedef struct RegSnapshot {
	uint64_t rdi, rsi, rdx, rcx, r8, r9;
	uint64_t rsp_at_entry;
	uint64_t rbp_at_entry;
	uint64_t rax_at_entry; // useful for variadic ABI (%al trick uses low 8 bits of RAX)
} RegSnapshot;

extern RegSnapshot g_variadic_entry_snapshot;

void variadic_observer_impl(const char* tag, int count, ...);
void variadic_observer(const char* tag, int count, ...); /* wrapper(asm) */

void probe_regs_sysv(RegSnapshot* out);

/* 
 * Demo functions for:
 * 1. Normal fixed-art call
 * 2. Floating-point args(we won't capture XMM regs in assembly in this basic version)
 * 3. Struct passing/returning
 * 4. Variadic calls: observe %al at entry (how many vector regs were used)
 */

typedef struct Pair32 {
	uint32_t a;
	uint32_t b;
} Pair32;

typedef struct Triple64 {
	uint64_t x;
	uint64_t y;
	uint64_t z;
} Triple64;

Pair32 make_pair32(uint32_t a, uint32_t b);
Triple64 make_triple64(uint64_t x, uint64_t y, uint64_t z);
uint64_t sum_u64_8(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);

/*
 * Variadic: we will capture RAX at entry to see %al (lower 8 bits).
 * On SysV AMD64, for calls to variadic functions, %al is used to indicate
 * the number of XMM registers used to pss floating-point arguments.
 *
 * We'll implement a function that reads %rax at entry (via probe) and prints it.
 */

void variadic_observer(const char* tag, int count, ...);

#else
# error "this lab code targets x86_64 SysV ABI. Build on Linux x86_64."
#endif
