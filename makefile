# Makefile for building server and client applications
INCDIR = ./inc
LIBS_SRCDIR = ./src
SERVER_SRCDIR = ./server-src
CLIENT_SRCDIR = ./client-src

CC = g++
CXXFLAGS = -Wall -Wextra -pedantic
LDFLAGS = $(foreach D, $(INCDIR), -I$(D))

# List all source files files
LIBS_SRCFILES = $(wildcard $(LIBS_SRCDIR)/*.cpp)
SERVER_SRCFILES = $(wildcard $(SERVER_SRCDIR)/*.cpp)
CLIENT_SRCFILES = $(wildcard $(CLIENT_SRCDIR)/*.cpp)

# List all object files
LIBS_OBJFILES = $(patsubst %.cpp, %.o, $(LIBS_SRCFILES))
SERVER_OBJFILES = $(patsubst %.cpp, %.o, $(SERVER_SRCFILES))
CLIENT_OBJFILES = $(patsubst %.cpp, %.o, $(CLIENT_SRCFILES))

# Default target
all: server client

# Target to build the server executable
server: $(SERVER_OBJFILES) $(LIBS_OBJFILES)
	$(CC) -o $@ $^ $(LDFLAGS)

# Target to build the client executable
client: $(CLIENT_OBJFILES) $(LIBS_OBJFILES)
	$(CC) -o $@ $^ $(LDFLAGS)

# Pattern rule to build object files from source files
%.o: %.cpp
	$(CC) $(CXXFLAGS) $(LDFLAGS) -c -o $@ $<

# Clean up generated files
clean:
	-rm -rf server client $(SERVER_OBJFILES) $(CLIENT_OBJFILES) $(LIBS_OBJFILES)

