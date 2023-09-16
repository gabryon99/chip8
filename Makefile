all:
	clang++ -I/opt/homebrew/include -L/opt/homebrew/lib -DDEBUG -Wall -Wextra -g -O0 -std=c++17 -lSDL2 -fsanitize=undefined src/main.cpp -o chip8