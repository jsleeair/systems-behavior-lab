Expectation: multiple definition of 'add' error in ld stage

jinsu@BOOK-HP30IE3R45:~/build-and-binary/00-translation-unit/step1-multiple-definition$ make
gcc -O0 -g -Wall -Wextra -c main.c -o main.o
gcc -O0 -g -Wall -Wextra -c a.c -o a.o
gcc -O0 -g -Wall -Wextra -c b.c -o b.o
gcc -O0 -g -Wall -Wextra main.o a.o b.o -o app
/usr/bin/ld: b.o: in function `add':
/home/jinsu/build-and-binary/00-translation-unit/step1-multiple-definition/add.h:4: multiple definition of `add'; a.o:/home/jinsu/build-and-binary/00-translation-unit/step1-multiple-definition/add.h:4: first defined here
collect2: error: ld returned 1 exit status
make: *** [Makefile:7: app] Error 1

gcc -E a.c | sed -n '1,120p'
# 1 "a.c"
# 1 "<built-in>"
# 1 "<command-line>"
# 31 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3 4
# 32 "<command-line>" 2
# 1 "a.c"
# 1 "add.h" 1



int add(int a, int b) {
 return a + b;
}
# 2 "a.c" 2
int fa(void) { return add(1, 2); }

gcc -E b.c | sed -n '1,120p'
# 1 "b.c"
# 1 "<built-in>"
# 1 "<command-line>"
# 31 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3 4
# 32 "<command-line>" 2
# 1 "b.c"
# 1 "add.h" 1



int add(int a, int b) {
 return a + b;
}
# 2 "b.c" 2
int fb(void) { return add(3, 4); }

Two add symbols -> linking error
nm a.o
0000000000000000 T add
0000000000000018 T fa
jinsu@BOOK-HP30IE3R45:~/build-and-binary/00-translation-unit/step1-multiple-definition$ nm b.o
0000000000000000 T add
0000000000000018 T fb
