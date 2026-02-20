#include <stdio.h>
#include "symbols.h"

/*
 * main() exists mainly to create an executable that keeps symbols around.
 * The "touch_symbols()" call ensures relevant symbols are referenced.
 *
 * NOTE:
 * We are not measuring performance here; this is purely structural insepction:
 * - symbol binding/visibility/type
 * - object vs executable symbol table
 */

int main(void) {
	touch_symbols();

	int a = 10, b = 3;
	int r1 = global_add(a, b);
	int r2 = hidden_mul(a, b);
	int r3 = weak_sub(a, b);

	printf("global_add=%d, hidden_mul=%d, weak_sub=%d\n", r1, r2, r3);
	printf("g_counter=%d, g_hidden_flag=%d, g_weak_value=%d, g_const_magic=%d\n", g_counter, g_hidden_flag, g_weak_value, g_const_magic);

	return 0;
}
