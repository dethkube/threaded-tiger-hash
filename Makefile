CFLAGS=-Wall -Werror
CPPFLAGS=-Wall -Werror

all: p2

p2: p2.o tiger.o sboxes.o smartalloc.o
	gcc -O2 -o $@ $^ -pthread -lm

p2.o: p2.c p2.h

tiger.o: tiger.c tiger.h

sboxes.o: sboxes.c

smartalloc.o: smartalloc.c smartalloc.h

clean:
	-rm -f p2 *.o

handin:
	handin bellardo 453_p2 p2.c p2.h tiger.c tiger.h sboxes.c smartalloc.c smartalloc.h Makefile README report.pdf
