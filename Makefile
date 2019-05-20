# Makefile
# Guilherme Dantas

CFLAGS = -w

all: testq main prog

main: prog main.o queue.o semlib.o
	$(CC) $(DEBUG) -o main main.o queue.o semlib.o $(CFLAGS) -lpthread 
	
testq: test_queue.o queue.o
	$(CC) $(DEBUG) -o testq test_queue.o queue.o $(CFLAGS)
	
prog: prog.c
	$(CC) $(DEBUG) -o prog prog.c $(CFLAGS)

main.o: main.c queue.h semlib.h
	$(CC) $(DEBUG) -o main.o main.c -c $(CFLAGS)

test_queue.o: test_queue.c queue.h
	$(CC) $(DEBUG) -o test_queue.o test_queue.c -c $(CFLAGS)

queue.o: queue.c queue.h
	$(CC) $(DEBUG) -o queue.o queue.c -c $(CFLAGS)

semlib.o: semlib.c semlib.h
	$(CC) $(DEBUG) -o semlib.o semlib.c -c $(CFLAGS)

zombies: doer caller

doer: doer.c
	$(CC) $(DEBUG) -o doer doer.c $(CFLAGS)

caller: caller.o semlib.o queue.o
	$(CC) $(DEBUG) -o caller caller.o semlib.o queue.o $(CFLAGS) -lpthread

caller.o: caller.c semlib.h queue.h
	$(CC) $(DEBUG) -o caller.o caller.c -c $(CFLAGS)

clean:
	# Deletes binaries and objects
	find . -type f -executable -exec sh -c "file -i '{}' | grep -q 'x-executable; charset=binary'" \; -print | xargs rm -f
	rm *.o
	
debug: DEBUG = -D _DEBUG -g
debug: all
