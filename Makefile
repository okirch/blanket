
CFLAGS	= -Wall -g -fPIC -D_GNU_SOURCE
LDFLAGS	= -shared -Wl,-soname -Wl,libblanket.so.0
LIBS	= -ldl

LIBOBJS	= context.o control.o sampling.o kernel.o hooks.o

all:	libblanket.so

clean:
	rm -f *.o libblanket.so

libblanket.so: $(LIBOBJS)
	$(CC) $(LDFLAGS) -o $@ $(LIBOBJS) $(LIBS)
