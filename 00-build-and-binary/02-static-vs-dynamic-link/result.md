# 02 - Static vs Dynamic Linking

## Objective

Demonstrate and verify the structural and runtime differences between:

- Linking against a shared object (`.so`)
- Linking against a static archive (`.a`)

The comparison focuses on:

- Binary size
- Runtime dependency
- ELF dynamic section
- Symbol placement
- Runtime loader behavior

---

## 1. Binary Size Comparison

Command:

```
size artifacts/bin/main_dynamic artifacts/bin/main_staticlib
```

Result:

```
   text    data     bss     dec     hex filename
   2124     648       8    2780     adc artifacts/bin/main_dynamic
   2120     608       8    2736     ab0 artifacts/bin/main_staticlib
```

### Interpretation

* The total size difference is small because the library is tiny.
* However:

  * `main_dynamic` contains only references to `libgreet.so`.
  * `main_staticlib` contains the actual object code of `greet()` inside the executable.

With a larger library, static linking would significantly increase executable size.

---

## 2. Runtime Dependency (ldd)

### main_dynamic

```
libgreet.so => .../artifacts/lib/libgreet.so
libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
```

### main_staticlib

```
libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
```

### Interpretation

* `main_dynamic` depends on `libgreet.so` at runtime.
* `main_staticlib` does **not** depend on `libgreet.so`.

This proves:

> Dynamic linking requires the shared object to be present at runtime.
> Static linking embeds the library code into the executable.

---

## 3. ELF Dynamic Section (readelf -d)

### main_dynamic

```
(NEEDED) Shared library: [libgreet.so]
(NEEDED) Shared library: [libc.so.6]
(RUNPATH) Library runpath: [$ORIGIN/../lib]
```

### main_staticlib

```
(NEEDED) Shared library: [libc.so.6]
```

### Interpretation

* `main_dynamic` explicitly declares `libgreet.so` in the `DT_NEEDED` entries.
* It also contains a `RUNPATH`, instructing the loader where to search.
* `main_staticlib` has **no reference** to `libgreet.so`.

This confirms the structural ELF-level difference.

---

## 4. Symbol Table Inspection (nm)

### main_dynamic

```
U greet
0000000000004010 B greet_counter
```

### main_staticlib

```
0000000000001213 T greet
0000000000004014 B greet_counter
```

### Interpretation

* In `main_dynamic`, `greet` is marked `U` (Undefined).

  * It will be resolved by the dynamic loader from `libgreet.so`.
* In `main_staticlib`, `greet` is marked `T` (Text section).

  * The function body is physically inside the executable.

This directly proves that static linking copies the object code.

---

## 5. Runtime Failure Experiment

`libgreet.so` was temporarily removed.

Result:

```
error while loading shared libraries: libgreet.so: cannot open shared object file
```

Exit code: `127`

Meanwhile, `main_staticlib` continued to execute normally.

### Interpretation

This is the strongest behavioral proof:

* Dynamic linking fails if the shared object is unavailable.
* Static linking does not require the shared object at runtime.

---

## 6. Address Observation

Example output:

Dynamic:

```
Address of greet()        = 0x72ddd6dbd119
Address of greet_counter  = 0x61ed17694010
```

Static:

```
Address of greet()        = 0x6387df030213
Address of greet_counter  = 0x6387df033014
```

### Interpretation

* In the dynamic case, `greet()` resides in a shared object mapping.
* In the static case, `greet()` resides within the executable's own memory image.

---

## Final Conclusion

Static Linking (`.a`):

* Copies library object code into the executable.
* No runtime dependency on the shared object.
* Symbols appear as defined (`T`) in the executable.
* Larger binary (in general).

Dynamic Linking (`.so`):

* Executable contains only references.
* Requires shared object at runtime.
* Symbol resolution performed by the dynamic loader.
* Allows library replacement without recompilation.

---

## Key Insight

The difference is not merely conceptual.

It is observable at three levels:

1. ELF structure (`DT_NEEDED`)
2. Symbol table (`U` vs `T`)
3. Runtime behavior (loader failure)

This experiment demonstrates that static linking is a build-time code inclusion mechanism, while dynamic linking is a runtime symbol resolution mechanism.

```
