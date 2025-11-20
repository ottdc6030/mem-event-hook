.PHONY: all clean run_test

# "make" compiles only the shared library
# "make test" compiles only the test.c program and the hi.c program
# "make run_test" compiles everything and runs the test program with the library injected at runtime


CC = gcc
CFLAGS = -fPIC -Wall -g
LDFLAGS = -shared -ldl -pthread

# Output library
LIBNAME = liboverride.so

# Source files
SOURCES = define_override.c event_queue.c
OBJECTS = $(SOURCES:.c=.o)

# Test program
TEST_PROG = test
TEST_SRC = test.c

# Hello world program
HI_PROG = hi
HI_SRC = hi.c

# Log location
LD_PRELOAD_LOG=logs/

all: $(LIBNAME)

$(LIBNAME): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_PROG): $(TEST_SRC)
	$(CC) -o $@ $<

$(HI_PROG): $(HI_SRC)
	$(CC) -o $@ $<

run_test: $(LIBNAME) $(TEST_PROG) $(HI_PROG)
	LD_PRELOAD=./$(LIBNAME) LD_PRELOAD_LOG=./$(LD_PRELOAD_LOG) ./$(TEST_PROG)

clean:
	rm -f $(OBJECTS) $(LIBNAME) $(TEST_PROG) $(HI_PROG)
