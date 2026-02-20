# Analysis: Translation Units, Linkage, and Symbol Resolution in C

## 1. Objective

This experiment investigates the following question:

> Why does placing a function *definition* in a header file cause a “multiple definition” linker error?

The goal is to understand **which stage of the build pipeline creates the problem** and how symbol linkage rules govern the final executable.

---

## 2. Build Model Overview

The C build pipeline consists of distinct stages:

1. **Preprocessing** (`#include`, macros)
2. **Compilation** (each translation unit → object file)
3. **Linking** (symbol resolution across object files)

A crucial concept here is the **translation unit (TU)**:

* A translation unit is a `.c` file after preprocessing.
* Each `.c` file is compiled independently.
* The linker later merges object files and resolves symbols.

Understanding this separation is key to explaining the observed behavior.

---

## 3. Step-by-Step Analysis

---

## Step 1 – Multiple Definition (Intentional Failure)

### Observation

The linker error:

```
multiple definition of `add`
```

appears when `add()` is defined inside `add.h` and included in both `a.c` and `b.c`.

From the preprocessed outputs (`a.i` and `b.i`):

* The full function definition of `add()` appears in **both** translation units.

From `nm`:

* `a.o` defines `add`
* `b.o` defines `add`

### Root Cause

`#include` is not linking — it is **textual inclusion** during preprocessing.

Therefore:

* Each `.c` file receives its own copy of the `add()` definition.
* Each object file exports a global symbol `add`.
* The linker encounters **two global definitions** of the same symbol.
* The One Definition Rule for global symbols is violated.

### Conclusion

The linker error is not caused by the linker being strict.
It is caused by preprocessing creating duplicate global definitions.

---

## Step 2 – Declaration in Header, Definition in Single Source File

### Observation

* `add.h` now contains only a declaration:

  ```
  int add(int a, int b);
  ```
* `add.c` contains the single definition.

From `nm`:

* `a.o` and `b.o` contain `U add` (undefined reference).
* `add.o` contains `T add` (defined symbol).

### Root Cause

Now:

* Only one translation unit defines `add`.
* Other units merely reference it.
* The linker can resolve all references to a single definition.

### Conclusion

Correct separation:

* Headers contain **declarations**.
* Exactly one source file contains the **definition**.

This satisfies global symbol uniqueness during linking.

---

## Step 3 – `static` in Header (Internal Linkage)

### Observation

`add()` is declared as:

```
static int add(int a, int b) { ... }
```

From `readelf -s`:

* The symbol binding of `add` is `LOCAL`.
* Each object file contains its own local `add`.

### Root Cause

The `static` keyword changes linkage:

* The function gains **internal linkage**.
* The symbol is visible only within its translation unit.
* Even though multiple copies exist, they are not globally visible.
* The linker does not consider them conflicting.

### Important Insight

The problem was not “multiple definitions” per se.
The problem was **multiple global definitions**.

By making them local, we eliminate the conflict.

### Tradeoff

* Code duplication may occur.
* Larger binary size is possible.
* No cross-TU sharing of implementation.

---

## Step 4 – `static inline` in Header

### Observation

Using:

```
static inline int add(int a, int b) { ... }
```

* Behavior is similar to Step 3 regarding linkage.
* With optimization (`-O2`), function calls may disappear (inlining).
* With `-O0`, the function may still appear as a local symbol.

### Key Insight

`inline` is an optimization hint, not a linkage modifier.
`static` is what prevents linker conflicts.

### Practical Pattern

`static inline` is the safe and common idiom for small header-defined functions.

---

## 4. What This Experiment Proves

This experiment demonstrates:

1. `#include` performs textual substitution before compilation.
2. Each `.c` file forms an independent translation unit.
3. The linker requires unique global symbol definitions.
4. Duplicate global definitions cause link-time failure.
5. `static` changes symbol visibility to internal linkage.
6. `inline` affects optimization, not linkage (by itself).

---

## 5. Conceptual Model (Mental Framework)

We can model the system as follows:

```
Preprocessor → Compiler (per TU) → Linker (global resolution)
```

Errors in Step 1 originate in:

* Preprocessing behavior.

Fixes in Step 2:

* Respect global symbol uniqueness.

Fixes in Step 3:

* Change symbol visibility.

This layered understanding is essential for:

* Debugging linker errors
* Designing header APIs
* Understanding binary size growth
* Analyzing performance implications of inlining

---

## 6. Broader Implications

This experiment is not about a trivial linker error.

It establishes foundational understanding of:

* Symbol tables
* Object file structure
* Linkage semantics
* Translation unit isolation
* Build system behavior

Without this mental model, performance engineering and systems debugging remain guesswork.

With it, symbol conflicts and binary behavior become predictable.

---

## 7. Final Statement

The multiple-definition error is not a compiler mystery.

It is a deterministic consequence of:

* Textual inclusion,
* Independent translation units,
* Global symbol resolution rules.

Understanding this boundary between preprocessing, compilation, and linking is a prerequisite for serious systems work.

--

