#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <stddef.h>

/* This header declares a small set of symbols intentionally crafted
 * to demonstrate how the ELF symbol table records:
 *
 * - Binding: LOCAL / GLOBAL / WEAK
 * - Visibility: DEFAULT / HIDDEN
 * - Type: FUNC / OBJECT
 * - Section placement: .text / .data / .bss / .rodata (often inferred)
 *
 * We will inspect these using:
 * 	readelf -sw
 * 	nm -a
 * 	objdump -t
 */

// A normal global function (GLOBAL binding, DEFAULT visibility).
int global_add(int a, int b);

// A global function but marked HIDDEN visibility (still GLOBAL binding).
__attribute__((visibility("hidden")))
int hidden_mul(int a, int b);

/* A weak function definition.
 * If some other object provides a strong definition of weak_sub(),
 * the strong one wins at link time.
 */
__attribute__((weak))
int weak_sub(int a, int b);

/* A weak *undefined* reference pattern is also common,
 * but here we'll keep it as a defined weak symbol for clarity.
 */

// A normal global variable (OBJECT, GLOBAL).
extern int g_counter;

// A global variable with HIDDEN visibility (OBJECT, GLOBAL, HIDDEN).
__attribute__((visibility("hidden")))
extern int g_hidden_flag;

// A weak global variable definition
__attribute__((weak))
extern int g_weak_value;

/* A const global often ends up in .rodata and may be shown as OBJECT.
 * Tool output can vary based on compiler, flags, and whether it is used.
*/
extern const int g_const_magic;

/* "static" symbols are internal to a translation unit (LOCAL binding).
 * We'll define them in symbols.c and do not expose them here.
 */

void touch_symbols(void);

#endif
