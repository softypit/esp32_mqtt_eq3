PROG ?= example
CFLAGS ?= -DMG_ENABLE_LINES $(EXTRA)
ROOT ?= $(realpath $(CURDIR)/../..)
LIN = docker run -it --rm -v $(ROOT):$(ROOT) -w $(CURDIR) mdashnet/cc2

ifneq ($(MBEDTLS),)
CFLAGS += -DMG_ENABLE_MBEDTLS=1 -I$(MBEDTLS)/include -I/usr/include
CFLAGS += -L$(MBEDTLS)/lib -lmbedtls -lmbedcrypto -lmbedx509
endif

ifneq ($(OPENSSL),)
CFLAGS += -DMG_ENABLE_OPENSSL=1 -I$(OPENSSL)/include
CFLAGS += -L$(OPENSSL)/lib -lssl -lcrypto
endif

all: $(PROG)
	$(RUN) ./$(PROG) $(ARGS)

$(PROG): main.c
	$(CC) ../../mongoose.c -I../.. -W -Wall $(CFLAGS) -o $(PROG) main.c

linux: all
linux: CFLAGS += -O2 -g -fsanitize=address,undefined,shift,null,return,bounds,alignment,object-size,bool,enum -static-libasan
linux: CC = $(LIN) cc
linux: RUN = $(LIN)

clean:
	rm -rf $(PROG) *.o *.dSYM *.gcov *.gcno *.gcda *.obj *.exe *.ilk *.pdb
