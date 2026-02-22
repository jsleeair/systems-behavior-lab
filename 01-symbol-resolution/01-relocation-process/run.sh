#!/usr/bin/env bash

#####################################################################
# Relocation Process Lab Runner
# 
# This script automates the 01-relocation-process experiment workflow.
#
# It intentionally separates:
#   1. Compilation phase (object generation)
#   2. Binary inspection phase (relocation + symbol table analysis)
#   3. Link phase (symbol resolution)
#
# The goal is NOT just to build and executable,
# but to observe how undefined symbols and relocation entries evolve
# across compilation and linking stages.
#
# This script produces artifacts that serve as experimental evidence.
#####################################################################

set -euo pipefail

#####################################################################
# Clean previous artifacts
#
# We remove the entire artifacts directory to ensure:
#   - No stale object files
#   - No mixed optimization builds
#   _ No misleading inspection results
#####################################################################
rm -rf artifacts
mkdir -p artifacts

#####################################################################
# STEP 1 - Compile main.c to objects files
#
# We compile main.c separately to preserve relocation entries.
# Linking is intentionally postponed.
#
# Two optimization levels are used:
#   - O0 : minimal optimization (maximal relocation visibility)
#   - O2 : aggressive optimization (may remove or alter relocations)
#####################################################################

echo "Compiling main.c with -O0..."
gcc -O0 -c src/main.c -o artifacts/main_O0.o

echo "Compiling main.c with -O2..."
gcc -O2 -c src/main.c -o artifacts/main_O2.o

#####################################################################
# STEP 2 - Compile ext.c (symbol provider)
#
# ext.o contains the definitions for:
#   - ext_func
#   - ext_var
#
# These will resolve undefined references during linking.
#####################################################################

echo "Compiling ext.c..."
gcc -O0 -c src/ext.c -o artifacts/ext.o

#####################################################################
# STEP 3 - Inspect relocation entries BEFORE linking
#
# readelf -r shows relocation entries in the object file.
#
# These entries represent:
#   "Places in the code that must be patched later"
#
# Expected:
#   - relocations for ext_func
#   - relocations for ext_var
#   - relocations for printf
#####################################################################

echo "Inspecting relocation entries (O0)..."
readelf -rW artifacts/main_O0.o > artifacts/main_O0.reloc.txt

echo "Inspecting relocation entries (O2)..."
readelf -rW artifacts/main_O2.o > artifacts/main_O2.reloc.txt

#####################################################################
# STEP 4 - Inspect symbol tables
#
# We confirm that:
#   - ext_func, ext_var, printf appear as UND (undefined)
#   - main appears as GLOBAL
#
# This proves that the compiler does not resolve addresses.
#####################################################################

echo "Inspecting symbol table (O0)..."
readelf -sW artifacts/main_O0.o > artifacts/main_O0.sym.txt

echo "Inspecting symbol table (O2)..."
readelf -sW artifacts/main_O2.o > artifacts/main_O2.sym.txt

#####################################################################
# STEP 5 - Disassemble with relocation annotations
#
# objdump -d   -> disassembly
# objdump -r   -> show relocations
# -C           -> demangle (useful in C++)
#
# This allows mapping:
#   relocation offset -> specific instruction
#
# You should see relocation comments next to:
#   - call ext_func
#   - load ext_var
#   - call printf
#####################################################################

echo "Disassembling with relocation info (O0)..."
objdump -drwC artifacts/main_O0.o > artifacts/main_O0.disasm.txt

echo "Disassembling with relocation info (O2)..."
objdump -drwC artifacts/main_O2.o > artifacts/main_O2.disasm.txt

#####################################################################
# STEP 6 - Link object files
#
# Now we resolve undefined symbols by linking main.o with ext.o.
#
# The linker:
#   - Matches undefined symbols with definitions
#   - Applies relocation patches
#   - Produces a final executable
#
# We also generate a linker map file for inspection.
#####################################################################

echo "Linking objects (O0)..."
gcc artifacts/main_O0.o artifacts/ext.o \
	-Wl,-Map=artifacts/a.out_O0.map.txt \
	-o artifacts/a.out_O0

echo "Linking objects (O2)..."
gcc artifacts/main_O2.o artifacts/ext.o \
	-Wl,-Map=artifacts/a.out_O2.map.txt \
	-o artifacts/a.out_O2
#####################################################################
# STEP 7 - Inspect symbol table AFTER linking
#
# Now ext_func and ext_var should be defined.
#
# printf may still appear as dynamic (depending on linking mode).
#####################################################################

echo "Inspecting final executable symbols(O0)..."
readelf -sW artifacts/a.out_O0 > artifacts/a.out_O0.sym.txt

echo "Inspecting final executable symbols(O2)..."
readelf -sW artifacts/a.out_O2 > artifacts/a.out_O2.sym.txt

#####################################################################
# STEP 8 - Optional: Inspect remaining relocations in executable
#
# In dynamically linked binaries, some relocations may remain.
# These are resolved by the dynamic loader at runtime.
#####################################################################

echo "Inspecting relocations in final executable(O0)..."
readelf -rW artifacts/a.out_O0 > artifacts/a.out_O0.reloc.txt || true

echo "Inspecting relocations in final executable(O2)..."
readelf -rW artifacts/a.out_O2 > artifacts/a.out_O2.reloc.txt || true

#####################################################################
# DONE
#
# At this point, the artifacts directory contains:
#   - Object files
#   - Relocation dumps
#   - Symbol tables
#   - Disassembly
#   - Linker map
#   - Final executable
#
# This directory is your experimental evidence.
#####################################################################

echo "Relocation experiment complete."
echo "Artifacts generated in ./artifacts"
