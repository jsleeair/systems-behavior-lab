# Static vs Dynamic Linking — What Actually Changes?

In this lab, we move beyond the textbook definition of static and dynamic linking.

Instead of saying:

> "Static linking copies code. Dynamic linking loads at runtime."

We **prove it structurally and behaviorally** using ELF inspection tools.

---

## Experimental Setup

We built a small library:

- `libgreet.so` (shared object)
- `libgreet.a`  (static archive)

And two executables:

- `main_dynamic` — linked against `libgreet.so`
- `main_staticlib` — linked against `libgreet.a`

The goal is to observe what changes at:

1. Binary size level  
2. ELF structure level  
3. Symbol table level  
4. Runtime loader behavior  

---

# Binary Size Comparison

```

size main_dynamic main_staticlib

```
```

text    data     bss     dec     hex
2124     648       8    2780     adc  main_dynamic
2120     608       8    2736     ab0  main_staticlib

```

The size difference is small because the library is tiny.

But conceptually:

- `main_staticlib` contains the actual function body of `greet()`
- `main_dynamic` only contains references

With larger libraries, this difference becomes significant.

---

# Runtime Dependency (ldd)

### Dynamic

```

libgreet.so => .../libgreet.so
libc.so.6

```

### Static (library only)

```

libc.so.6

```

Key observation:

- `main_dynamic` requires `libgreet.so` at runtime.
- `main_staticlib` does not.

This already proves that dynamic linking introduces a runtime dependency.

---

# ELF Dynamic Section

Using:

```

readelf -d

```

### main_dynamic

```

(NEEDED) Shared library: [libgreet.so]
(RUNPATH) Library runpath: [$ORIGIN/../lib]

```

### main_staticlib

```

(NEEDED) Shared library: [libc.so.6]

```

The dynamic executable explicitly records:

- Which shared libraries it needs
- Where to find them

The static-linked executable does not reference `libgreet.so` at all.

This is not a runtime behavior difference —  
it is a **structural ELF difference**.

---

# 4️⃣ Symbol Table Difference

```

nm -an

```

### main_dynamic

```

U greet

```

### main_staticlib

```

T greet

```

Meaning:

- `U` → Undefined symbol (resolved by the loader)
- `T` → Defined in the executable text section

This is the clearest structural proof that:

> Static linking copies object code into the executable.

Dynamic linking does not.

---

# Runtime Failure Experiment

We temporarily removed `libgreet.so`.

Result:

```

error while loading shared libraries: libgreet.so: cannot open shared object file

```

- `main_dynamic` → failed (exit code 127)
- `main_staticlib` → still worked

This confirms:

Dynamic linking is a runtime resolution mechanism.

Static linking is a build-time code inclusion mechanism.

---

# Memory Address Observation

Dynamic:

```

Address of greet() = 0x72ddd6dbd119

```

Static:

```

Address of greet() = 0x6387df030213

```

In the dynamic case, the function lives inside a shared object mapping.

In the static case, it lives inside the executable’s own memory image.

---

# Final Takeaways

| Aspect | Static Library (.a) | Shared Object (.so) |
|--------|---------------------|----------------------|
| Code location | Inside executable | Separate file |
| Runtime dependency | No | Yes |
| Symbol resolution | Build time | Runtime |
| ELF `DT_NEEDED` entry | No | Yes |
| Failure if library missing | No | Yes |

---

# What This Really Means

Static linking is not just “old style linking.”

It fundamentally changes:

- The ELF structure
- The symbol table
- The runtime behavior
- The loader’s responsibilities

Dynamic linking shifts responsibility from the linker to the runtime loader.

And that shift becomes critical when we later analyze:

- PLT / GOT
- Lazy binding
- Relocations
- Position Independent Code (PIC)

---

# Next Step

Now that we understand *what* changes structurally,

the next question becomes:

**How does the dynamic loader actually resolve those undefined symbols?**

That leads directly into:

> PIC and GOT/PLT analysis.
```
