make
gcc -O0 -g -Wall -Wextra -c main.c -o main.o
gcc -O0 -g -Wall -Wextra -c a.c -o a.o
gcc -O0 -g -Wall -Wextra -c b.c -o b.o
gcc -O0 -g -Wall -Wextra main.o a.o b.o -o app
jinsu@BOOK-HP30IE3R45:~/build-and-binary/00-translation-unit/step3-static-in-header$ ls
Makefile  a.c  a.o  add.h  app  b.c  b.o  main.c  main.o  result.md
jinsu@BOOK-HP30IE3R45:~/build-and-binary/00-translation-unit/step3-static-in-header$ ./app
3 7

jinsu@BOOK-HP30IE3R45:~/build-and-binary/00-translation-unit/step3-static-in-header$ nm a.o
0000000000000000 t add
0000000000000018 T fa
jinsu@BOOK-HP30IE3R45:~/build-and-binary/00-translation-unit/step3-static-in-header$ nm b.o
0000000000000000 t add
0000000000000018 T fb

Symbol table '.symtab' contains 16 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FILE    LOCAL  DEFAULT  ABS a.c
     2: 0000000000000000     0 SECTION LOCAL  DEFAULT    1
     3: 0000000000000000     0 SECTION LOCAL  DEFAULT    2
     4: 0000000000000000     0 SECTION LOCAL  DEFAULT    3
     5: 0000000000000000    20 FUNC    LOCAL  DEFAULT    1 add

jinsu@BOOK-HP30IE3R45:~/systems-behavior-lab/build-and-binary/00-translation-unit/step4-static-inline-in-header$ objdump -d /tmp/a_O0.o | sed -n '/<fa>/,/^$/p'
0000000000000014 <fa>:
  14:   f3 0f 1e fa             endbr64
  18:   55                      push   %rbp
  19:   48 89 e5                mov    %rsp,%rbp
  1c:   be 02 00 00 00          mov    $0x2,%esi
  21:   bf 01 00 00 00          mov    $0x1,%edi
  26:   e8 d5 ff ff ff          callq  0 <add>
  2b:   5d                      pop    %rbp
  2c:   c3                      retq
jinsu@BOOK-HP30IE3R45:~/systems-behavior-lab/build-and-binary/00-translation-unit/step4-static-inline-in-header$ objdump -d /tmp/a_O2.o | sed -n '/<fa>/,/^$/p'
0000000000000000 <fa>:
   0:   f3 0f 1e fa             endbr64
   4:   b8 03 00 00 00          mov    $0x3,%eax
   9:   c3                      retq


