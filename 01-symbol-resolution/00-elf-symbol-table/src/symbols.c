#include "symbols.h"

/* 
 * Global variable definitions.
 * These will appear in the ELF symbol table with GLOBAL binding.
 */
int g_counter = 7;
__attribute__((visibility("hidden")))
int g_hidden_flag = 1;

__attribute__((weak))
int g_weak_value = 123;

/*
 * A const global definition. Usually ends up in .rodata.
 * (Whether it appears as a symbol and how it is marked can vary.)
 */
const int g_const_magic = 0XC0FFEE;

/*
 * Translation-unit local symbols:
 * - static function -> LOCAL binding, type FUNC
 * - static variable -> LOCAL binding, type OBJECT
 *
 * They are not visible to other translation units.
 */
static int local_add(int a, int b) {
	return a + b;
}

static int local_state = 42;

// Normal global function.
int global_add(int a, int b) {
	/* 
	 * Use local_state and local_add so they cannot be optimized away easily.
	 * Still, some optimizations may inline things; we keep -O0 by default.
	 */
	local_state += 1;
	return local_add(a, b) + local_state;
}

/*
 * Hidden visibility global function.
 * "hidden" affects dynamic symbol visibility and interposition, but the symbol
 * still exists in the object file's symbol table.
 */
int hidden_mul(int a, int b) {
	return a * b;
}

/*
 * Weak function definition.
 * A strong definition provided elsewhere will override this one during linking.
 */
int weak_sub(int a, int b) {
	return a - b;
}

/*
 * A helper function to ensure various symbols are "touched"
 * so they stay in the final binary and can be insepcted
 */
void touch_symbols(void) {
	// Prevent unused warnings and keep references alive.
	volatile int sink = 0;
	sink += global_add(1, 2);
	sink += hidden_mul(2, 3);
	sink += weak_sub(10, 4);

	sink += g_counter;
	sink += g_hidden_flag;
	sink += g_weak_value;
	sink += g_const_magic;

	// Also reference TU-local objects indirectly by calling global_add().
	(void)sink;
}

