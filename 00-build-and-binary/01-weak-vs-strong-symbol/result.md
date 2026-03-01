# 01-weak-vs-strong-symbol — Result

## 1. Objective

This experiment investigates how the linker resolves symbols under the following scenarios:

* Strong vs Strong
* Weak vs Strong
* Weak vs Weak (implicitly observed)
* Tentative definitions under `-fcommon`
* Tentative definitions under `-fno-common`

The goal is to confirm symbol resolution behavior using:

* Runtime output
* `nm`
* `readelf -s`
* Section header inspection
* Linker error messages

---

# 2. Experimental Evidence

---

## Case 1 — Weak Variable vs Strong Variable

### Runtime Output

```
g_value = 222
[weak hook] default implementation
```

### nm Evidence

```
0000000000004014 D g_value
```

* `D` → Initialized global data (.data section)
* Indicates strong data symbol

### readelf Symbol Table

```
name=g_value value=0000000000004014 size=4 type=OBJECT bind=GLOBAL vis=DEFAULT ndx=25
```

* `type=OBJECT`
* `bind=GLOBAL` → strong binding
* `ndx=25`

### Section Mapping

From section headers:

```
[25] .data
```

### Interpretation

* The weak definition (`__attribute__((weak)) int g_value = 111`) was discarded.
* The strong definition (`int g_value = 222`) was selected.
* Final symbol resides in `.data`.

**Conclusion: Strong symbol overrides weak symbol.**

---

## Case 2 — Weak Function vs Strong Function

### Runtime Output

```
g_value = 111
[strong hook] overridden implementation
```

### nm Evidence

```
00000000000011ad T hook
```

* `T` → Text section (strong function)

### readelf Symbol Table

```
name=hook value=00000000000011ad size=23 type=FUNC bind=GLOBAL vis=DEFAULT ndx=16
```

* `type=FUNC`
* `bind=GLOBAL` → strong binding
* `ndx=16`

### Section Mapping

```
[16] .text
```

### Interpretation

* The weak function definition was ignored.
* The strong function definition replaced it.
* Final symbol resides in `.text`.

**Conclusion: Strong function overrides weak function.**

---

## Case 3 — Strong vs Strong

### Linker Error

```
multiple definition of `dup'
first defined here
```

### Interpretation

When two strong definitions of the same symbol exist:

* The linker cannot choose one.
* Linking fails with a multiple definition error.

**Conclusion: Strong + Strong → Link Failure**

---

## Case 4 — Tentative Definitions with `-fcommon`

Source:

```c
int common;
```

Compiled with `-fcommon`.

### nm Evidence

```
0000000000000004 C common
```

* `C` → COMMON symbol

### readelf Evidence

```
name=common value=0000000000000004 size=4 type=OBJECT bind=GLOBAL vis=DEFAULT ndx=COM
```

* `ndx=COM` → COMMON symbol (not placed in .bss yet)
* Symbol is mergeable across object files

### Interpretation

Under `-fcommon`:

* Tentative definitions do not become real definitions.
* They remain COMMON symbols.
* The linker merges them successfully.

**Conclusion: `-fcommon` allows tentative definitions to co-exist.**

---

## Case 5 — Tentative Definitions with `-fno-common`

### Linker Error

```
multiple definition of `common'
first defined here
```

### Interpretation

Under `-fno-common`:

* Tentative definitions become real definitions.
* Multiple definitions are treated as strong symbols.
* Linker fails.

**Conclusion: `-fno-common` enforces strict definition rules.**

---

# 3. Symbol Type Mapping (nm ↔ readelf)

| nm | Meaning          | readelf Interpretation    |
| -- | ---------------- | ------------------------- |
| T  | Text (function)  | FUNC + GLOBAL + .text     |
| D  | Initialized data | OBJECT + GLOBAL + .data   |
| C  | COMMON           | OBJECT + GLOBAL + ndx=COM |
| W  | Weak function    | FUNC + WEAK               |
| V  | Weak object      | OBJECT + WEAK             |

---

# 4. Final Conclusions

1. Strong definitions override weak definitions.
2. Two strong definitions cause link failure.
3. Weak definitions act as replaceable defaults.
4. `-fcommon` keeps tentative definitions in COMMON state.
5. `-fno-common` turns tentative definitions into real definitions, causing strict duplicate checks.

---

# 5. Key Takeaway

Weak symbols are *fallback candidates*.

Strong symbols are *authoritative definitions*.

COMMON symbols are *unresolved global placeholders* that may be merged unless strict mode (`-fno-common`) is used.

---
