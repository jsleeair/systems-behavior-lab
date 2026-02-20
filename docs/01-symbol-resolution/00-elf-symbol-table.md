# 00 — ELF Symbol Table: Binding and Visibility

## Why This Experiment Exists

The linker and dynamic loader do not “interpret” C semantics directly.

They make decisions based on metadata stored in the ELF symbol table.

This experiment inspects how different C linkage properties map into:

- Binding (LOCAL / GLOBAL / WEAK)
- Visibility (DEFAULT / HIDDEN)
- Type (FUNC / OBJECT)
- Section placement (.text / .data / .bss / .rodata)

The goal is not just to observe output,
but to understand what information drives symbol resolution.

---

## Experimental Setup

Source code intentionally defines:

- `static` functions and variables
- Normal global symbols
- `__attribute__((weak))` symbols
- `__attribute__((visibility("hidden")))` symbols
- `const` global data

Built with:

```bash
gcc -O0 -g

Inspection tools:

```bash
readelf -sW
nm -a
objdump -t
```

Reproducible experiment:
See repository folder:

```
01-symbol-resolution/00-elf-symbol-table
```

---

## Key Observations

### static → LOCAL binding

Symbols defined as `static`:

```c
static int local_add(int a, int b);
static int local_state;
```

Observed in `readelf -sW symbols.o` as:

* Binding: LOCAL
* Type: FUNC or OBJECT
* Not visible outside this translation unit

Meaning:

These symbols never participate in inter-object resolution.
They exist only within the object file that defines them.

---

### Normal global → GLOBAL binding

Example:

```c
int global_add(int a, int b);
int g_counter;
```

Observed as:

* Binding: GLOBAL
* Visibility: DEFAULT
* Type: FUNC or OBJECT

Meaning:

These symbols are visible to the linker and can satisfy references
from other object files.

They are candidates for symbol resolution.

---

### weak → WEAK binding

Example:

```c
__attribute__((weak))
int weak_sub(int a, int b);
```

Observed as:

* Binding: WEAK

Meaning:

Weak symbols can be overridden by a strong definition
in another object file.

Resolution priority rule:

STRONG > WEAK

The linker selects a strong definition if present.

---

### hidden visibility

Example:

```c
__attribute__((visibility("hidden")))
int hidden_mul(int a, int b);
```

Observed as:

* Binding: GLOBAL
* Visibility: HIDDEN

Meaning:

The symbol exists in the symbol table,
but it cannot be interposed dynamically.

It is not exported for external dynamic linkage.

Important distinction:

Binding controls resolution strength.
Visibility controls exposure outside the shared object.

---

### Section placement

Using `objdump -t`, symbols map into sections:

* Functions → `.text`
* Initialized globals → `.data`
* Uninitialized globals → `.bss`
* Const globals → `.rodata`

The ELF symbol table includes section indices,
allowing the linker and loader to know
where each symbol resides in memory.

---

## What Drives Symbol Resolution?

The linker does not rely on C keywords directly.

It relies on fields stored in the ELF symbol table:

* Binding
* Visibility
* Type
* Section index

Symbol resolution behavior is fully determined
by this metadata.

C keywords such as `static` and attributes such as `weak`
are simply ways to encode linkage policy into ELF metadata.

---

## Mental Model Shift

Before this experiment:

* `static` felt like a C scoping rule.
* `weak` felt like a niche compiler feature.

After inspecting ELF metadata:

* They are explicit encodings of resolution policy.
* Symbol resolution is a deterministic metadata-driven process.
* The linker is not "smart" — it follows symbol table rules.

---

## Why This Matters for Systems Work

Understanding symbol binding and visibility is foundational for:

* Static vs dynamic linking behavior
* Symbol interposition
* Shared library design
* ABI stability
* Debugging multiple definition errors
* Performance tuning (e.g., PLT/GOT behavior)

This experiment establishes the structural basis
for deeper topics such as relocation and PLT/GOT mechanics.

---

## Next Step

Having identified how symbols are described,
the next logical question is:

How are symbol references relocated and bound at link time and runtime?

That leads to:

**01 — Relocation Process**

