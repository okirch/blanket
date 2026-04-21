
CFLAGS	= -Wall -g -fPIC -D_GNU_SOURCE
LDFLAGS	= -shared -Wl,-soname -Wl,libblanket.so.0
LIBS	= -ldw -lelf -ldl

LIBOBJS	= context.o control.o object.o sampling.o kernel.o hooks.o

UTILOBJS= blanket.o coverage.o elf.o object.o control.o dwarf.o

all:	libblanket.so blanket

clean:
	rm -f *.o libblanket.so

libblanket.so: $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(LIBOBJS) $(LIBS)

blanket: $(UTILOBJS)
	$(CC) $(CCFLAGS) -o $@ $(UTILOBJS) $(LIBS)

depend:
	sed -e '/^## Automated/,$$d' Makefile > Makefile.tmp
	echo "## Automated dependencies: ##" >>Makefile.tmp
	gcc -MM *.c >>Makefile.tmp
	mv Makefile.tmp Makefile

## Automated dependencies: ##
blanket.o: blanket.c blanket.h
context.o: context.c blanket.h
control.o: control.c blanket.h
coverage.o: coverage.c blanket.h
elf.o: elf.c blanket.h
hooks.o: hooks.c blanket.h
kernel.o: kernel.c blanket.h
object.o: object.c blanket.h
sampling.o: sampling.c blanket.h
