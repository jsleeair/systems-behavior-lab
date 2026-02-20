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
     5: 0000000000000000    24 FUNC    LOCAL  DEFAULT    1 add
