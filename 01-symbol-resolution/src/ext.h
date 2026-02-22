#pragma once

/*
 * This header declares external symbols that will be referenced
 * from another translation unit(main.c).
 *
 * The purpose is to intentionally create undefined symbol references
 * during compilation of main.c, so that relocation entries will be
 * genderated in main.o
 *
 * These declarations do NOT define the symbols.
 * They only inform the compiler about their existence.
 *
 * The actual definitions live in ext.c.
 */

/*
 * External global variable.
 *
 * Accessing this from main.c will genderate a relocation entry
 * because the compiler does not know its final address at compile time.
 */
extern int ext_var;

/*
 * External function declaration.
 *
 * Calling this function from main.c will generate a relocation
 * entry in the .text section because the target address is unknown
 * at compile time.
 */ 
int ext_func(int x);
