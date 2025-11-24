.PHONY: all clean run_test

# "make" compiles only the shared library
# "make test" compiles only the test.c program and the hi.c program
# "make run_test" compiles everything and runs the test program with the library injected at runtime


CC = gcc
CXX = g++
CFLAGS = -fPIC -Wall
CXXFLAGS = -fPIC -Wall -std=c++11
LDFLAGS = -shared -ldl -pthread -lstdc++

# Output library
LIBNAME = liboverride.so

# Source files
C_SOURCES = define_override.c event_queue.c
CPP_SOURCES = alloc_map.cpp
C_OBJECTS = $(C_SOURCES:.c=.o)
CPP_OBJECTS = $(CPP_SOURCES:.cpp=.o)
OBJECTS = $(C_OBJECTS) $(CPP_OBJECTS)

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
	$(CXX) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TEST_PROG): $(TEST_SRC)
	$(CC) -o $@ $<

$(HI_PROG): $(HI_SRC)
	$(CC) -o $@ $<

run_test: $(LIBNAME) $(TEST_PROG) $(HI_PROG)
	LD_PRELOAD=./$(LIBNAME) LD_PRELOAD_LOG=./$(LD_PRELOAD_LOG) ./$(TEST_PROG)

clean:
	rm -f $(OBJECTS) $(LIBNAME) $(TEST_PROG) $(HI_PROG)
