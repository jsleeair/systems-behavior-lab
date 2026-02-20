### 'result.md'
```md
# Result - 00-elf-symbol-table
```bash
grep -E 'global_add|hidden_mul|weak_sub|g_counter|g_hidden_flag|g_weak_value|g_const_magic|local_add|local_state|touch_symbols' \
adelf_s_symbols_>   artifacts/readelf_s_symbols_o.txt
```
     6: 0000000000000000    24 FUNC    LOCAL  DEFAULT    1 local_add
     7: 000000000000000c     4 OBJECT  LOCAL  DEFAULT    3 local_state
    17: 0000000000000000     4 OBJECT  GLOBAL DEFAULT    3 g_counter
    18: 0000000000000004     4 OBJECT  GLOBAL HIDDEN     3 g_hidden_fla
    19: 0000000000000008     4 OBJECT  WEAK   DEFAULT    3 g_weak_value
    20: 0000000000000000     4 OBJECT  GLOBAL DEFAULT    5 g_const_magi
    21: 0000000000000018    58 FUNC    GLOBAL DEFAULT    1 global_add
    22: 0000000000000052    23 FUNC    GLOBAL HIDDEN     1 hidden_mul
    23: 0000000000000069    22 FUNC    WEAK   DEFAULT    1 weak_sub
    24: 000000000000007f   149 FUNC    GLOBAL DEFAULT    1 touch_symbol

```bash
grep -E 'global_add|hidden_mul|weak_sub|g_counter|g_hidden_flag|g_weak_value|g_const_magic|local_add|local_state|touch_symbols' \
adelf_s_app.txt>   artifacts/readelf_s_app.txt
```
    43: 00000000000011f2    24 FUNC    LOCAL  DEFAULT   16 local_add
    44: 000000000000401c     4 OBJECT  LOCAL  DEFAULT   25 local_state
    48: 0000000000001244    23 FUNC    LOCAL  DEFAULT   16 hidden_mul
    58: 000000000000120a    58 FUNC    GLOBAL DEFAULT   16 global_add
    60: 000000000000207c     4 OBJECT  GLOBAL DEFAULT   18 g_const_magi
    62: 0000000000004018     4 OBJECT  WEAK   DEFAULT   25 g_weak_value
    64: 0000000000004014     4 OBJECT  GLOBAL HIDDEN    25 g_hidden_fla
    73: 0000000000004010     4 OBJECT  GLOBAL DEFAULT   25 g_counter
    78: 000000000000125b    22 FUNC    WEAK   DEFAULT   16 weak_sub
    79: 0000000000001271   149 FUNC    GLOBAL DEFAULT   16 touch_symbol

```bash
grep -E 'global_add|hidden_mul|weak_sub|g_counter|g_hidden_flag|g_weak_value|g_const_magic|local_add|local_state|touch_symbols' \
>   artifacts/nm_symbols_o.txt
```
0000000000000000 R g_const_magic
0000000000000000 D g_counter
0000000000000004 D g_hidden_flag
0000000000000008 V g_weak_value
0000000000000018 T global_add
0000000000000052 T hidden_mul
0000000000000000 t local_add -> lowercase
000000000000000c d local_state -> lowercase
000000000000007f T touch_symbols
0000000000000069 W weak_sub

```bash
grep -E 'global_add|hidden_mul|weak_sub|g_counter|g_hidden_flag|g_weak_value|g_const_magic|local_add|local_state|touch_symbols' \
jdump_t_symbols_>   artifacts/objdump_t_symbols_o.txt
```
0000000000000000 l     F .text  0000000000000018 local_add
000000000000000c l     O .data  0000000000000004 local_state
0000000000000000 g     O .data  0000000000000004 g_counter
0000000000000004 g     O .data  0000000000000004 .hidden g_hidden_flag
0000000000000008  w    O .data  0000000000000004 g_weak_value
0000000000000000 g     O .rodata        0000000000000004 g_const_magic
0000000000000018 g     F .text  000000000000003a global_add
0000000000000052 g     F .text  0000000000000017 .hidden hidden_mul
0000000000000069  w    F .text  0000000000000016 weak_sub
000000000000007f g     F .text  0000000000000095 touch_symbols

## Question
How do binding (LOCAL/GLOBAL/WEAK) and visibility (DEFAULT/HIDDEN) appear in ELF symbol tables?

## Hypothesis
- 'static' -> LOCAL binding
- non-static global -> GLOBAL binding
- '__attribute__((weak))' -> WEAK binding
- '__attribute__(visibility("hidden")))' -> Visibility = HIDDEN (still GLOBAL binding)

## Root-cause
The linker/loader decides how a symbol can be referenced and overridden based on ELF symbol table fields: **binding (LOCAL/GLOBAL/WEAK)** and **visibility (DEFAULT/HIDDEN)**, plus **type/section index** indicating whether it is a function or object and where it lives.

## Preproduction
```bash
./run.sh
