.PHONY: all clean run_test

# "make" compiles only the shared library
# "make test" compiles only the test.c program
# "make run_test" compiles both and runs the test program with the library injected at runtime


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

# Log location
LD_PRELOAD_LOG=mem-events.json

all: $(LIBNAME)

$(LIBNAME): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_PROG): $(TEST_SRC)
	$(CC) -o $@ $<

run_test: $(LIBNAME) $(TEST_PROG)
	LD_PRELOAD=./$(LIBNAME) LD_PRELOAD_LOG=./$(LD_PRELOAD_LOG) ./$(TEST_PROG)

clean:
	rm -f $(OBJECTS) $(LIBNAME) $(TEST_PROG)
