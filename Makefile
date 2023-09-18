CXX=clang++
CXXFLAGS=-std=c++17 -Wall -Wextra

DEBUG_FLAGS=-DDEBUG -Wall -Wextra -g -O0 -fsanitize=undefined
RELEASE_FLAGS=-O2

RELEASE=bin/release/
DEBUG=bin/debug/
EXE=chip8

LDFLAGS=-lSDL2

all: src/main.cpp
	mkdir -p $(RELEASE)
	$(CXX) $(CXXFLAGS) $(RELEASE_FLAGS) $(LDFLAGS) -I/opt/homebrew/include -L/opt/homebrew/lib src/main.cpp -o $(RELEASE)/$(EXE)

debug: src/main.cpp
	mkdir -p $(DEBUG)
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(LDFLAGS) -I/opt/homebrew/include -L/opt/homebrew/lib src/main.cpp -o $(DEBUG)/$(EXE)


clean:
	rm -rf bin/*