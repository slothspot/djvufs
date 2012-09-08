CC=gcc
CXX=g++
CFLAGS=-c -g -Wall $(shell pkg-config --cflags fuse libpng)
LDFLAGS=$(shell pkg-config --libs fuse libpng) -ldjvulibre -ljpeg
SOURCES=djvufs.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=djvufs
RM=rm -rf

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

.cpp.o:
	$(CXX) $(CFLAGS) $< -o $@

clean:
	$(RM) $(EXECUTABLE) $(OBJECTS)

