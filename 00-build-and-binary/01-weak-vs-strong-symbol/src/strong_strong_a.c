/*
 * Defines a STRONG symbol "dup".
 * We'll link this together with strong_strong_b.c which defines the same symbol,
 * to intentionally trigger a multiple-definition linker error.
 */

int dup = 1;
