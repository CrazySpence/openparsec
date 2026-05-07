CFILES = makeodt.cpp getopt.cpp

CFLAGS = -g -O2 -x c++ -fpermissive -I../BspLib/ -I../QvLib/

all: makeodt

clean:
	rm -f *.o *~ makeodt

makeodt: $(CFILES:.c=.o) ../BspLib/libbsp.a ../QvLib/libqv.a
	$(CC) -o makeodt $(CFILES:.c=.o) ../BspLib/libbsp.a ../QvLib/libqv.a -lstdc++ $(CFLAGS)

# use this if you've compiled BspLib with JPEG support
#makeodt: $(CFILES:.c=.o) ../BspLib/libbsp.a ../QvLib/libqv.a
#	$(CC) -o makeodt $(CFILES:.c=.o) ../BspLib/libbsp.a ../QvLib/libqv.a -lstdc++ -ljpeg



