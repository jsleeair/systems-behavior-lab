#include "greet.h"
#include <stdio.h>

// Definition of the global variable declared in greet.h
int greet_counter = 0;

void greet(const char *name) {
	/*
	 * This function is intentionally simple.
	 * The point of the lab is not what it does, *where it lives*
	 * (inside the executable vs in a shared object loaded at runtime).
	 */
	greet_counter++;

	if (name == NULL) {
		name = "(null)";
	}

	printf("[libgreet] Hello, %s! (greet_counter=%d)\n", name, greet_counter);
}
