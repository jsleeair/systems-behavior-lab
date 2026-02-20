# 00-translation-unit

## Question
Why does putting a function *definition* in a header cause "multiple definition" link errors?

## Key claim
`#include` is textual inclusion (preprocessing), not linking.

## Steps
- step1: intentionally break (definition in header)
- step2: fix (declaration in header, single definition in add.c)
- step3: static in header (local symbols per TU)
- step4: static inline (observe inlining with -O0 vs -O2)

## Evidence
- `gcc -E` shows header contents pasted into each translation unit
- `nm` / `readelf -s` shows duplicate symbol definitions

