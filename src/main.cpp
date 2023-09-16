#include <SDL2/SDL.h>
#include <_types/_uint16_t.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ratio>
#include <stdexcept>
#include <vector>

#include "SDL2/SDL_error.h"
#include "SDL2/SDL_events.h"
#include "SDL2/SDL_render.h"
#include "SDL2/SDL_timer.h"
#include "SDL2/SDL_video.h"

#define CARRY_FLAG 0x0f

#define PACK16(msb, lsb) (((uint16_t)(msb) << 0x08) | lsb)

#define FIRST_NIBBLE(data) ((data >> 0x0c) & 0x0f)
#define SECOND_NIBBLE(data) ((data >> 0x08) & 0x0f)
#define THIRD_NIBBLE(data) ((data >> 0x04) & 0x0f)
#define FOURTH_NIBBLE(data) ((data >> 0x00) & 0x0f)

#define LSB(data) ((data & 0xff))
#define MSB(data) ((data >> 0x08) & 0xff)

#define TWELVE(data) (data & 0xfff)

namespace utility {
template <typename T>
void PrintHex(T value, uint8_t trailing = 4) {
    std::cerr << "0x" << std::setfill('0') << std::setw(trailing) << std::right << std::hex << value
              << std::resetiosflags(std::ios::showbase);
}
}  // namespace utility

namespace chip8 {

namespace debugger {};

namespace graphics {
namespace fonts {
using Font = std::array<uint8_t, 80>;
constexpr Font DEFAULT = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,  // 0
    0x20, 0x60, 0x20, 0x20, 0x70,  // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0,  // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0,  // 3
    0x90, 0x90, 0xF0, 0x10, 0x10,  // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0,  // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0,  // 6
    0xF0, 0x10, 0x20, 0x40, 0x40,  // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0,  // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0,  // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90,  // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0,  // B
    0xF0, 0x80, 0x80, 0x80, 0xF0,  // C
    0xE0, 0x90, 0x90, 0x90, 0xE0,  // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0,  // E
    0xF0, 0x80, 0xF0, 0x80, 0x80   // F
};
}  // namespace fonts

namespace colors {

struct Color {
    const uint8_t r;
    const uint8_t g;
    const uint8_t b;
    const uint8_t a;

    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r{r}, g{g}, b{b}, a{a} {}
};

constexpr Color BLACK = {0x00, 0x00, 0x00, 0xff};
constexpr Color WHITE = {0xff, 0xff, 0xff, 0xff};
constexpr Color RED = {0xff, 0x00, 0x00, 0xff};
constexpr Color GREEN = {0x00, 0xff, 0x00, 0xff};
constexpr Color BLUE = {0x00, 0x00, 0xff, 0xff};
}  // namespace colors

}  // namespace graphics

struct Config {
    uint32_t scaleFactor{20};
    bool useScanline{true};
    chip8::graphics::colors::Color scanline{0x0f, 0x0f, 0x0f, 0xff};
    chip8::graphics::colors::Color fgColor = chip8::graphics::colors::RED;
    chip8::graphics::colors::Color bgColor = chip8::graphics::colors::BLACK;
};

namespace display {
constexpr std::uint32_t DISPLAY_WIDTH = 64;
constexpr std::uint32_t DISPLAY_HEIGHT = 32;

class Screen {
    std::array<bool, DISPLAY_WIDTH * DISPLAY_HEIGHT> data{};
    SDL_Window* windowHandle{nullptr};
    SDL_Renderer* renderer{nullptr};
    Config config;

   public:
    Screen(Config c, const char* title = "Chip8++") : config{c} {
        std::fill_n(data.begin(), data.size(), 0);
        if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
            throw std::runtime_error{SDL_GetError()};
        }
        windowHandle = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        DISPLAY_WIDTH * config.scaleFactor, DISPLAY_HEIGHT * config.scaleFactor, 0);
        if (windowHandle == nullptr) {
            throw std::runtime_error{SDL_GetError()};
        }
        renderer = SDL_CreateRenderer(windowHandle, -1, SDL_RENDERER_ACCELERATED);
        if (renderer == nullptr) {
            throw std::runtime_error{SDL_GetError()};
        }
    }

    ~Screen() noexcept {
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (windowHandle != nullptr) {
            SDL_DestroyWindow(windowHandle);
        }
        SDL_Quit();
    }

    void CleanScreen() {
        auto [r, g, b, a] = config.bgColor;
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderClear(renderer);
    }

    template <typename Callback>
    void PollEvent(Callback callback) {
        SDL_Event e{};
        while (SDL_PollEvent(&e)) {
            callback(e);
        }
    }

    bool ReadPixel(std::size_t x, std::size_t y) { return data[DISPLAY_HEIGHT * x + y]; }

    void Draw(std::size_t x, std::size_t y, bool value) { data[DISPLAY_HEIGHT * x + y] = value; }

    void Delay(uint32_t deltaTime = 0) { SDL_Delay(16 + deltaTime); }

    void Update() {
        CleanScreen();

        // Draw pixels on screen
        auto [r, g, b, a] = config.fgColor;

        for (std::size_t x = 0; x < DISPLAY_WIDTH; x++) {
            for (std::size_t y = 0; y < DISPLAY_HEIGHT; y++) {
                SDL_Rect rect{};
                rect.x = x * config.scaleFactor;
                rect.y = y * config.scaleFactor;
                rect.h = config.scaleFactor;
                rect.w = config.scaleFactor;
                if (ReadPixel(x, y)) {
                    SDL_SetRenderDrawColor(renderer, r, g, b, a);
                    SDL_RenderFillRect(renderer, &rect);
                }
                if (config.useScanline) {
                    auto [r, g, b, a] = config.scanline;
                    SDL_SetRenderDrawColor(renderer, r, g, b, a);
                    SDL_RenderDrawRect(renderer, &rect);
                }
            }
        }
        SDL_RenderPresent(renderer);
    }
};

}  // namespace display

namespace system {

struct Cpu {
    static constexpr size_t STARTING_PC = 0x200;

    /// Points at current instruction in memory.
    std::size_t PC{STARTING_PC};
    /// Stack Pointer
    std::uint8_t SP{0};
    /// Whic points to used instruction in memory (can address only 12 bits).
    uint16_t I{0};
    /// Decremented at rate of 60hz until it reaches 0.
    uint8_t delayTimer{0};
    /// Decremented at rate of 60hz until it reaches 0.
    uint8_t soundTimer{0};
    /// Named V0, V1, V2, ..., VF (used as flag register).
    std::array<uint8_t, 0x10> V;
    /// The stack is an aray of 16bit 16 value (that means up to 16 subroutines nested).
    std::array<uint16_t, 0x10> stack;

    explicit Cpu() {
        std::fill_n(V.begin(), V.size(), 0);
        std::fill_n(stack.begin(), stack.size(), 0);
    }
};

class Memory {
    static constexpr std::size_t MEMORY_SIZE = 1 << 12;  /// 4KiB
    std::array<uint8_t, MEMORY_SIZE> data{0};

   public:
    constexpr uint8_t Read8(const std::size_t address) const { return data[address]; }

    constexpr uint16_t Read16(const std::size_t address) const { return PACK16(data[address], data[address + 1]); }

    constexpr void Write8(const std::size_t address, const uint8_t value) { data[address] = value; }

    constexpr void Write16(const std::size_t address, const uint16_t value) {
        uint8_t msb = (value >> 8) & 0xff;
        uint8_t lsb = (value >> 0) & 0xff;
        data[address] = msb;
        data[address + 1] = lsb;
    }

    template <size_t Size>
    constexpr void WriteBytes(const std::array<uint8_t, Size> input, const std::size_t offset) {
        if (input.size() + offset >= MEMORY_SIZE) {
            throw std::invalid_argument{"The data to write could not be stored."};
        }
        auto dest = data.begin();
        std::advance(dest, offset);
        std::copy_n(input.begin(), input.size(), dest);
    }

    void WriteBytes(const std::vector<uint8_t>&& input, const std::size_t offset = 0) {
        if (input.size() + offset >= MEMORY_SIZE) {
            throw std::invalid_argument{"The data to write could not be stored."};
        }
        auto dest = data.begin();
        std::advance(dest, offset);
        std::copy_n(input.begin(), input.size(), dest);
    }
};

}  // namespace system

class Emulator {
    enum class Status { RUNNING, PAUSED, STOPPED };

    Config config{};

    chip8::system::Cpu cpu;
    chip8::system::Memory memory;
    chip8::display::Screen screen{config};

    Status currentStatuts{Status::PAUSED};

    void Jump(uint16_t instr, bool hasOffset = false) { 
        auto offset = (hasOffset) ? cpu.V[0] : 0;
        cpu.PC = TWELVE(instr) + offset;
     }

    void LoadIntoV(uint16_t instr) {
        auto reg = SECOND_NIBBLE(instr);
        assert(0 <= reg && reg < 0xf0);
        auto value = LSB(instr);
        cpu.V[reg] = value;
    }

    void SetIndexRegister(uint16_t instr) {
        cpu.I = TWELVE(instr);  // SET Index Register I (0xANNN)
    }

    void ClearScreen(uint16_t) {
        for (std::size_t x = 0; x < chip8::display::DISPLAY_WIDTH; x++) {
            for (std::size_t y = 0; y < chip8::display::DISPLAY_HEIGHT; y++) {
                screen.Draw(x, y, false);
            }
        }
    }

    void Call(uint16_t instr) {
        auto address = TWELVE(instr);
        cpu.stack[cpu.SP++] = cpu.PC;
        cpu.PC = address;
        std::cerr << "[info] calling routine at: 0x" << std::hex << address << std::endl;
    }

    void ReturnFromRoutine(uint16_t) {
        // Return from Subroutine
        cpu.PC = cpu.stack[cpu.SP--];
    }

    void Assignment8(uint16_t instr) {
        // 1->OR, 2->AND, 3->XOR
        // 8XY1, 8XY2, 8XY3
        uint8_t x = SECOND_NIBBLE(instr);
        uint8_t y = THIRD_NIBBLE(instr);
        uint8_t op = FOURTH_NIBBLE(instr);

        switch (op) {
            case 0x0: {
                cpu.V[x] = cpu.V[y];
                break;
            }
            case 0x1: {
                cpu.V[x] |= cpu.V[y];
                break;
            }
            case 0x2: {
                cpu.V[x] &= cpu.V[y];
                break;
            }
            case 0x3: {
                cpu.V[x] ^= cpu.V[y];
                break;
            }
            case 0x4: {
                std::cerr << "[error] :: unimplemented operator\n";
                std::exit(-1);
                break;
            }
            case 0x5: {
                std::cerr << "[error] :: unimplemented operator\n";
                std::exit(-1);
                break;
            }
            case 0x6: {
                std::cerr << "[error] :: unimplemented operator\n";
                std::exit(-1);
                break;
            }
            case 0x7: {
                std::cerr << "[error] :: unimplemented operator\n";
                std::exit(-1);
                break;
            }
            case 0xE: {
                std::cerr << "[error] :: unimplemented operator\n";
                std::exit(-1);
                break;
            }
            default: {
                std::cerr << "[error] :: unimplemented operator\n";
                std::exit(-1);
                break;
            }
        }
    }

    void Add(uint16_t instr) {
        auto reg = SECOND_NIBBLE(instr);
        assert(0 <= reg && reg < 0xf0);
        cpu.V[reg] += LSB(instr);
    }

    void SkipEqual(uint16_t instr, bool compareRegister) {
        // 4xkk/5xy0
        // Skip next instruction if Vx != kk.
        auto reg = SECOND_NIBBLE(instr);
        auto value = (compareRegister) ? (cpu.V[THIRD_NIBBLE(instr)]) : LSB(instr);
        if (cpu.V[reg] == value) {
            cpu.PC += 2;
        }
    }

    void SkipNotEqual(uint16_t instr, bool compareRegister) {
        // 4xkk
        // Skip next instruction if Vx != kk.
        auto reg = SECOND_NIBBLE(instr);
        auto value = (compareRegister) ? (cpu.V[THIRD_NIBBLE(instr)]) : LSB(instr);
        if (cpu.V[reg] != value) {
            cpu.PC += 2;
        }
    }

    void DrawPixels(uint16_t instr) {
        const uint8_t x = cpu.V[SECOND_NIBBLE(instr)] % (chip8::display::DISPLAY_WIDTH);
        const uint8_t y = cpu.V[THIRD_NIBBLE(instr)] % (chip8::display::DISPLAY_HEIGHT);
        const uint8_t n = FOURTH_NIBBLE(instr);
        uint8_t y0 = y;

        cpu.V[CARRY_FLAG] = 0;

        for (std::size_t i = 0; i < n; i++) {
            const uint8_t spriteRow = memory.Read8(cpu.I + i);
            uint8_t x0 = x;

            for (int8_t j = 7; j >= 0; j--) {
                bool pixel = screen.ReadPixel(x0, y0);
                uint8_t spriteBit = (spriteRow & (1 << j));
                if (spriteBit && pixel) {
                    cpu.V[CARRY_FLAG] = 0x1;
                }
                screen.Draw(x0, y0, pixel ^ spriteBit);
                if (++x0 >= chip8::display::DISPLAY_WIDTH) break;
            }

            if (++y0 >= chip8::display::DISPLAY_HEIGHT) break;
        }
    }

   public:
    void LoadFont(const chip8::graphics::fonts::Font font) { memory.WriteBytes(font, 0x50); }

    void LoadRom(const std::vector<uint8_t>&& rom) {
        memory.WriteBytes(std::move(rom), chip8::system::Cpu::STARTING_PC);
    }

    void Run() {
        while (currentStatuts != Status::STOPPED) {
            screen.PollEvent([](SDL_Event& event) {
                if (event.type == SDL_QUIT) {
                    std::exit(EXIT_FAILURE);
                }
                if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                        std::exit(EXIT_FAILURE);
                    }
                }
            });

            auto start = std::chrono::steady_clock::now();

            // Fecth the next instruction. The instruction has 4 nibbles.
            uint16_t instr = memory.Read16(cpu.PC);
            cpu.PC += 2;

            // Decode the instruction
            uint8_t opcode = FIRST_NIBBLE(instr);
            switch (opcode) {
                case 0x0: {
                    // Clear Screen
                    if (instr == 0x00E0) ClearScreen(instr);
                    if (instr == 0x00EE) ReturnFromRoutine(instr);
                    break;
                }
                case 0x1: {
                    Jump(instr);
                    break;
                }
                case 0x2: {
                    Call(instr);
                    break;
                }
                case 0x3: {
                    SkipEqual(instr, false);
                    break;
                }
                case 0x4: {
                    SkipNotEqual(instr, false);
                    break;
                }
                case 0x5: {
                    SkipEqual(instr, true);
                    break;
                }
                case 0x6: {
                    LoadIntoV(instr);
                    break;
                }
                case 0x7: {
                    Add(instr);
                    break;
                }
                case 0x8: {
                    Assignment8(instr);
                    break;
                }
                case 0x9: {
                    SkipNotEqual(instr, true);
                    break;
                }
                case 0xA: {
                    SetIndexRegister(instr);
                    break;
                }
                case 0xB: {
                    Jump(instr, true);
                    break;
                }
                case 0xD: {
                    DrawPixels(instr);
                    break;
                }
                default: {
                    std::cerr << "[error] :: Not implemented yet: 0x" << std::hex << instr << ".\n";
                    std::exit(-1);
                }
            }

            auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);

            screen.Delay();
            screen.Update();
        }
    }
};

}  // namespace chip8

std::vector<uint8_t> ReadBinaryFile(const char* filename) {
    std::ifstream file(filename, std::ios::binary);
    file.unsetf(std::ios::skipws);

    std::streampos fileSize;

    file.seekg(0, std::ios::end);
    fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // reserve capacity
    std::vector<uint8_t> vec{};
    vec.reserve(fileSize);

    // read the data:
    vec.insert(vec.begin(), std::istream_iterator<uint8_t>(file), std::istream_iterator<uint8_t>());

    return vec;
}

int main(const int argc, const char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: chip8 ./path/to/rom\n";
        return EXIT_FAILURE;
    }

    auto rom = ReadBinaryFile(argv[1]);

    chip8::Emulator emulator{};
    emulator.LoadFont(chip8::graphics::fonts::DEFAULT);
    emulator.LoadRom(std::move(rom));

    emulator.Run();

    return EXIT_SUCCESS;
}