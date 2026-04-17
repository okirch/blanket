
CFLAGS = -Wall -g
UTILS	= threads dlopen mcount

all:	$(UTILS)

clean:
	rm -f *.o $(UTILS)

dlopen: dlopen.o
	$(CC) -o $@ dlopen.o -ldl

mcount: mcount.o
	$(CC) -pg -o $@ mcount.o

mcount.o: mcount.c
	$(CC) $(CFLAGS) -pg -c -o $@ $<

threads: threads.o
	$(CC) -o $@ threads.o -lpthread -lm

# We want to align the thread functions so that reporting shows (almost) the same
# coverage for both function1 and 2.
threads.o: threads.c
	$(CC) $(CFLAGS) -falign-functions=64 -c -o $@ $<
