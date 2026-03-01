/*
 * Defines a WEAK function. This is a common "default hook" pattern:
 * provide a default implementation that projects/products may override.
 */

#include <stdio.h>

__attribute__((weak)) void hook(void) {
	printf("[weak hook] default implementation\n");
}
