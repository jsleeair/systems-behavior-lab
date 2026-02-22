#include <stdio.h>
#include "ext.h"

/*
 * This file is intentionally designed to generate relocation entires.
 *
 * During compilation (-c), the compiler does NOT know:
 *   - The final address of ext_func
 *   - The final address of ext_var
 *   - The final address of printf (from libc)
 *
 * Therefore, main.o will contain:
 *   - Undefined (UND) symbols in the symbol table
 *   - Reloation entries in .rela.text (or .rel.text)
 *
 * The linker will later resolve these using ext.o and libc.
 */

int main(void) {

	/*
	 * 1. Function call relocation
	 *
	 * Calling ext_func creates a relocation entry because:
	 *   - The call instruction needs a target address
	 *   - The compiler does not yet know where ext_func will live
	 *
	 * In disassembly, you will likely see:
	 *   call <placeholder>
	 *
	 * In relocation table:
	 *   R_X86_64_PC32 or similar
	 */
	int value_from_func = ext_func(10);

	/*
	 * 2. Global variable access relocation
	 *
	 * Accessing ext_var also requires relocation because:
	 *   - Its address is not known during compilation
	 *   - The compliler emits a placeholder displacement or address
	 *
	 * Depending on PIE/non-PIE:
	 *   - RIP-relative relocation
	 *   - GOT-based relocation
	 */
	int value_from_var = ext_var;

	/*
	 * 3. libc function relocation
	 *
	 * printf is not defined in this translation unit.
	 * It is provided by libc.
	 *
	 * This creates:
	 *   - Undefined symbol entry in main.o
	 *   - Relocation entry in .text
	 *
	 * In dynamically linked executables,
	 * this may later involve PLT/GOT resolution.
	 */
	printf("Result: %d\n", value_from_func + value_from_var);

	return 0;
}
