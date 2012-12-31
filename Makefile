.PHONY: all
all: static gtksqltest

sources = $(wildcard gtk/*.c)
objects = $(sources:.c=.o)

test_sources = $(wildcard test/*.c)
test_objects = $(test_sources:.c=.o)

$(objects) $(test_objects): $(wildcard gtk/*.h)

CFLAGS = -Wall -O0 -g
CFLAGS += -I.
CFLAGS += $(shell pkg-config --cflags gtk+-3.0 sqlite3)

.PHONY: clean
clean:
	$(RM) src/*.o libgtksqlstore.a
	$(RM) test/*.o gtksqltest

libgtksqlstore.a: $(objects)
	$(AR) r $@ $?

static: libgtksqlstore.a

gtksqltest: $(test_objects) libgtksqlstore.a
	$(CC) $(CFLAGS) -o $@ $^ $(shell pkg-config --libs gtk+-3.0 sqlite3)

