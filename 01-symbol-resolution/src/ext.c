#include "ext.h"
/*
 * Definition of ext_var.
 *
 * This symbol will appear in ext.o as:
 *   - Binding: GLOBAL
 *   - Type: OBJECT
 *   - Section: .data
 *
 * When linking, references from main.o will be resolved
 * against this definition.
 */
int ext_var = 7;

/*
 * Definition of ext_func.
 *
 * This symbol will appear in ext.o as:
 *   - Binding: GLOBAL
 *   - Type: FUNC
 *   - Section: .text
 *
 * The function body is intentionally simple.
 * We want predictable assembly output.
 */
int ext_func(int x) {
	return x + ext_var;
}
