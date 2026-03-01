/*
 * "Tentative definition" (no initializer) of a global variable.
 * Historically, GCC would often emit this as a COMMON symbol (-fcommon behavior).
 * With -fno-common, this becomes a normal strong definition, so duplicates fail.
 */

int common; /* tentative definition */
