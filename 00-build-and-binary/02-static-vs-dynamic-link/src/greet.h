#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * A small library API to demonstrate static vs dynamic linking.
 *
 * We intentionally expose:
 *  - a function (greet)
 *  - a global variable (greet_counter)
 * so we can inspect symbols and see what ends up in the final executable.
 */

void greet(const char *name);

/*
 * Global state: increments each time greet() is called.
 * This makes it easy to observe that the library has data symbols too.
 */

extern int greet_counter;

#ifdef __cplusplus
}
#endif
