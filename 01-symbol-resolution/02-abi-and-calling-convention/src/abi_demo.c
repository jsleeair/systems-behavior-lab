#include "abi_demo.h"
#include <stdio.h>
#include <stdarg.h>

RegSnapshot g_variadic_entry_snapshot = {0};

Pair32 make_pair32(uint32_t a, uint32_t b) {
	Pair32 p = { .a = a, .b = b };
	return p;
}

Triple64 make_triple64(uint64_t x, uint64_t y, uint64_t z) {
	Triple64 t = { .x = x, .y = y, .z = z };
	return t;
}

uint64_t sum_u64_8(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
		uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) {
	/*
	 * This function intentionally takes 8 integer args:
	 * SysV AMD64 passes the first 6 integer/pointer args in regs (RDI..R9),
	 * and the rest (a6, a7) on the stack.
	 *
	 * We'll combine with a probe in the caller to observe the setup.
	 */
	return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
}

static void dump_snapshot(const char* title, const RegSnapshot* s) {
	printf("\n== %s == \n", title);
	printf(" rdi=%#018lx  rsi=%#018lx  rdx=%#018lx\n", s->rdi, s->rsi, s->rdx);
	printf(" rcx=%#018lx  r8 =%#018lx  R9 =%#018lx\n", s->rcx, s->r8, s->r9);
	printf(" rsp=%#018lx  rbp=%#018lx\n", s->rsp_at_entry, s->rbp_at_entry);
	printf(" rax(entry)=%#018lx  (al=%#04lx)\n", s->rax_at_entry, s->rax_at_entry & 0xff);
	printf(" rsp %% 16 = %lu\n", (unsigned long)(s->rsp_at_entry % 16));
}

/*
 * Variadic observer:
 * - We capture rax at entry via a probe call *immediately* at the beginning.
 * - For variadic calls on SysV AMD64, the caller sets %al to the number of XMM regs used
 *   to pass floating-point args. This matters because the callee needs to know where to 
 *   find unnamed floating args (register save area vs stack).
 */
void variadic_observer_impl(const char* tag, int count, ...) {
	dump_snapshot("variadic_observer entry snapshot", &g_variadic_entry_snapshot);

	printf(" tag=\"%s\" count=%d\n", tag, count);

	va_list ap;
	va_start(ap, count);

	/*
	 * We'll read 'count' arguments as either uint64_t or double depending on the tag.
	 * This is just to exercise mixed int/float varargs at the call site.
	 */
	if (tag && tag[0] == 'd') {
		for (int i = 0; i < count; i++) {
			double v = va_arg(ap, double);
			printf("  arg[%d] as double = %.6f\n", i, v);
		}
	} else {
		for (int i = 0; i < count; i++) {
			uint64_t v = va_arg(ap, uint64_t);
			printf("  arg[%d] as u64 = %#lx\n", i, (unsigned long)v);
		}
	}

	va_end(ap);
}
