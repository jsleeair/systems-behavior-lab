/*
 * Defines a STRONG function with the same name as weak_func.c.
 * This should override the weak function implementation.
 */

#include <stdio.h>

void hook(void) {
	printf("[strong hook] overridden implementation\n");
}
