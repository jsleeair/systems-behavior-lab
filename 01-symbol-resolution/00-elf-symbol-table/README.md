# 01-symbol-resolution / 00-elf-symbol-table

## Goal
Inspect how ELF symbol tables encode symbol properties that matter for linking/loading:
- Binding: LOCAL / GLOBAL / WEAK
- Visibility: DEFAULT / HIDDEN
- Type: FUNC / OBJECT
- Where symbols come from (per object file vs final executable)

## Repro
```bash
./run.sh
