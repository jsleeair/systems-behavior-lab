jinsu@BOOK-HP30IE3R45:~/build-and-binary/00-translation-unit/step2-fix-header-declare$ make
gcc -O0 -g -Wall -Wextra -c main.c -o main.o
gcc -O0 -g -Wall -Wextra -c a.c -o a.o
gcc -O0 -g -Wall -Wextra -c b.c -o b.o
gcc -O0 -g -Wall -Wextra -c add.c -o add.o
gcc -O0 -g -Wall -Wextra main.o a.o b.o add.o -o app
jinsu@BOOK-HP30IE3R45:~/build-and-binary/00-translation-unit/step2-fix-header-declare$ ./app
3 7

 grep -n "int add" a.i
11:int add(int a, int b);
jinsu@BOOK-HP30IE3R45:~/systems-behavior-lab/build-and-binary/00-translation-unit/step2-fix-header-declare/artifacts$ grep -n "int add" b.i
11:int add(int a, int b);
