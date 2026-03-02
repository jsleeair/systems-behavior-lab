#include "greet.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	const char *name = "world";
	if (argc >= 2) {
		name = argv[1];
	}

	printf("[main] About to call greet()...\n");
	greet(name);

	/*
	 * Print the address of a function and a global variable.
	 * In a dynamic link, these addresses typically come from a mapped .so.
	 * In a static-linked library scenario (for libgreet.a), they will point
	 * into the executable's own text/data segments.
	 */
	printf("[main] Address of greet()	= %p\n", (void *)&greet);
	printf("[main] Address of greet_counter	= %p\n", (void *)&greet_counter);

	// Touch the variable from main() so the linker cannot discard it.
	greet_counter += 100;
	printf("[main] After modification, greet_counter = %d\n", greet_counter);

	return 0;
}
