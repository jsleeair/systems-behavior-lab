/*
 * main.c - PLT/GOT / lazy binding observation lab
 *
 * Goal:
 *   Make a dynamically-linked call (printf) and observe how:
 *     - the call site goes through PLT (Procedure Linkage Table)
 *     - the target address is stored/patched in GOT (.got.plt)
 *     - the first call triggers the dynamic resolver (lazy binding)
 *     - subsequent calls jump directly to the resolved libc function
 *
 * Notes:
 *   - We keep code tiny and deterministic.
 *   - We'll build with -no-pie to simplify address reasoning in analysis tools.
 */

#include <stdio.h>

/*
 * foo() exists mainly to make an indirect call chain:
 *   main -> foo -> printf
 * so you can set breakpoints at foo() printf@plt and see the flow.
 */
static void foo(int n) {
	/*
	 * printf is a symbol that lives in libc.so (shared library).
	 * The executable itself does NOT contain printf implementation.
	 * Instead, it contains a PLT stub that eventually jumps into libc.
	 */
	printf("hello (%d)\n", n);
}

int main(void) {
	/*
	 * Two calls on purpose:
	 *   - First call should trigger lazy binding (resolver path)
	 *   - Second call should use the already-patched GOT entry (fast path)
	 */
	foo(1);
	foo(2);

	return 0;
}
