# 00 — Translation Unit, Linkage, and “Multiple Definition”

## Question
Why does putting a function *definition* in a header cause a linker error?

## Summary of steps
- Step1: Definition in header included by multiple `.c` files → **multiple definition** (expected)
- Step2: Declaration in header, single definition in `add.c` → **fixed**
- Step3: `static` in header → **internal linkage**, no conflict
- Step4: `static inline` + optimization boundary

## Key evidence
- `gcc -E` shows `#include` is textual inclusion (same function body appears in multiple translation units)
- `nm` shows whether a symbol is **defined** (T/t) or **required** (U)
- `readelf -s` shows symbol binding (**LOCAL vs GLOBAL**)
- `objdump -d` shows whether `call add` remains or disappears

## O0 vs O2 (step4)
Under `-O0`, `fa()` calls `add()`.

Under `-O2`, the compiler can inline and fold constants, reducing `fa()` to `return 3;`.

This is not just “call removal”; it enables downstream optimizations such as constant propagation and folding.

## Links
- Code: `build-and-binary/00-translation-unit/`
