#include "abi_demo.h"
#include <stdio.h>
#include <inttypes.h>

static void print_pair32(Pair32 p) {
	printf(" Pair32{ a=%" PRIu32 ", b=%" PRIu32 " }\n", p.a, p.b);
}

static void print_triple64(Triple64 t) {
	printf(" Triple64{ x=%#" PRIx64 ", y=%#" PRIx64 ", z=%#" PRIx64 " }\n", t.x, t.y, t.z);
}

int main(void) {
	printf("02-abi-and-calling-convention (SysV AMD64)\n");

	/*
	 * Experiment A: Observe the register convention for 6+ integer args.
	 * - We'll snapshot registers at entry to a probe called from main,
	 *   but more importantly, we will snapshot at entry to variadic_observer too.
	 * Note: prboe_regs_sysv itself uses RDI as its 1st argument (out pointer).
	 * So we mainly use it inside functions whose ABI behavior we want to inspect
	 * *at their own entry*, e.g., inside variadic_observer.
	 */

	printf("\n-- Experiment 1: 8 integer args (first 6 in regs, rest on stack) --\n");
	uint64_t s = sum_u64_8(0x1111, 0x2222, 0x3333, 0x4444,
			0x5555, 0x6666, 0x7777, 0x8888);
	printf(" sum_u64_8 result = %#" PRIx64 "\n", s);

	/*
	 * Experiment B: Struct return.
	 * Small structs are often returned in registers; larger ones may use memory.
	 * We'll compile at different optimization levels and inspect the generated code
	 */
	printf("\n-- Experiment 2: struct return --\n");
	Pair32 p = make_pair32(10, 20);
	print_pair32(p);

	Triple64 t = make_triple64(0xAAAABBBBCCCCDDDDULL, 0x1111222233334444ULL, 0x9999000011112222ULL);
	print_triple64(t);

	/*
	 * Experiment C: Variadic + floating-point args.
	 * The key: for variadic calls on SysV AMD64, %al indicates how many XMM regs were used.
	 * We'll call variadic_observer with doubles and see (al) printed from entry snapshot.
	 */
	printf("\n-- Experiment 3: variadic calls (observe %%al via rax) --\n");

	printf("\n[Call #1] tag='d'\n");
	variadic_observer("d", 4, 1.0, 2.0, 3.5, 4.25);

	printf("\n[Call #2] tag='u'\n");
	variadic_observer("u", 4, 0xAULL, 0xBULL, 0xCULL, 0xDULL);
	/*
	 * Note: If you want a cleaner, more controlled view:
	 * - Compare -O0 vs -O2
	 * - Add/remove -fno-omit-frame-pointer
	 * _ Look at objdump output in artifacts/
	 */
	printf("\nDone.\n");
	return 0;
}
