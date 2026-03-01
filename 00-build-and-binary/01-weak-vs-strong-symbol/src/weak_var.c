/*
 * Defines a WEAK global variable. If a strong definition of the same symbol name
 * exists in any other linked object, the strong one should override this.
 * If no other definition exists, this weak one will satisfy the reference.
 */

#include <stdio.h>

/* Weak definition: the linker may discard this if a strong definition exists. */
__attribute__((weak)) int g_value = 111;
