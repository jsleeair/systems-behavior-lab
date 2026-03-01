/*
 * This file references symbols that may be provided by different object files.
 * The purpose is to observe shich definition the linker chooses (weak vs strong),
 * and to reproduce link errors for multiple strong definitions.
 *
 * We intentionally keep the code simple: print a global variable and call a function.
 */

#include <stdio.h>

/*
 * A global variable we expect to be defined elsewhere.
 * Depending on what we link, this may resolve to a weak or strong definition.
 */
extern int g_value;

/*
 * A function we expect to be defined elsewhere.
 * Depending on what we linke, this may resolve to a weak or strong implementation.
 */
extern void hook(void);

int main(void) {
	printf("g_value = %d\n", g_value);
	hook();
	return 0;
}
