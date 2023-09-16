#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>

#include "SDL2/SDL_error.h"
#include "SDL2/SDL_events.h"
#include "SDL2/SDL_render.h"
#include "SDL2/SDL_timer.h"
#include "SDL2/SDL_video.h"

#define PACK16(first, second) (((uint16_t)(first) << 0x08) | second)

#define FIRST_NIBBLE(data) ((data >> 0x0c) & 0x0f)
#define SECOND_NIBBLE(data) ((data >> 0x08) & 0x0f)
#define THIRD_NIBBLE(data) ((data >> 0x04) & 0x0f)
#define FOURTH_NIBBLE(data) ((data >> 0x00) & 0x0f)

#define LSB(data) (data & 0x08)
#define MSB(data) ((data >> 0x08) & 0x08)

#define TWELVE(data) (data & 0xfff)

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

    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        : r{r}, g{g}, b{b}, a{a} {}
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
    chip8::graphics::colors::Color fgColor = chip8::graphics::colors::WHITE;
    chip8::graphics::colors::Color bgColor = chip8::graphics::colors::BLACK;
};

namespace display {
constexpr std::uint32_t DISPLAY_WIDTH = 64;
constexpr std::uint32_t DISPLAY_HEIGHT = 32;

class Screen {
    SDL_Window* windowHandle{nullptr};
    SDL_Renderer* renderer{nullptr};
    Config config;

   public:
    Screen(Config c, const char* title = "Chip8++") : config{c} {
        if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
            throw std::runtime_error{SDL_GetError()};
        }
        windowHandle = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        DISPLAY_WIDTH * config.scaleFactor,
                                        DISPLAY_HEIGHT * config.scaleFactor, 0);
        if (windowHandle == nullptr) {
            throw std::runtime_error{SDL_GetError()};
        }
        renderer =
            SDL_CreateRenderer(windowHandle, -1, SDL_RENDERER_ACCELERATED);
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

    void Delay(uint32_t deltaTime = 0) { SDL_Delay(16 + deltaTime); }

    void Update() { SDL_RenderPresent(renderer); }
};

}  // namespace display

namespace system {

struct Cpu {
    static constexpr size_t STARTING_PC = 0x200;

    /// Points at current instruction in memory.
    size_t programCounter{STARTING_PC};
    /// Whic points to used instruction in memory (can address only 12 bits).
    uint16_t indexRegister{0};
    /// Decremented at rate of 60hz until it reaches 0.
    uint8_t delayTimer{0};
    /// Decremented at rate of 60hz until it reaches 0.
    uint8_t soundTimer{0};
    /// Named V0, V1, V2, ..., VF (used as flag register).
    std::array<uint8_t, 0x10> generalPurposeRegisters{0};
};

class Memory {
    static constexpr std::size_t MEMORY_SIZE = 1 << 12;  /// 4KiB
    std::array<uint8_t, MEMORY_SIZE> data{0};

   public:
    constexpr uint8_t Read(const std::size_t address) const {
        return data[address];
    }

    template <size_t Size>
    constexpr void WriteBytes(const std::array<uint8_t, Size> input,
                              const std::size_t offset) {
        if (input.size() + offset >= MEMORY_SIZE) {
            throw std::invalid_argument{
                "The data to write could not be stored."};
        }
        auto dest = data.begin();
        std::advance(dest, offset);
        std::copy_n(input.begin(), input.size(), dest);
    }

    void WriteBytes(const std::vector<uint8_t>&& input,
                    const std::size_t offset = 0) {
        if (input.size() + offset >= MEMORY_SIZE) {
            throw std::invalid_argument{
                "The data to write could not be stored."};
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

   public:
    void LoadFont(const chip8::graphics::fonts::Font font) {
        memory.WriteBytes(font, 0x50);
    }

    void LoadRom(const std::vector<uint8_t>&& rom) {
        memory.WriteBytes(std::move(rom), chip8::system::Cpu::STARTING_PC);
    }

    void Run() {
        screen.CleanScreen();

        while (currentStatuts != Status::STOPPED) {
            screen.PollEvent([](SDL_Event& event) {
                if (event.type == SDL_QUIT) {
                    std::exit(EXIT_FAILURE);
                }
            });

            if (currentStatuts == Status::PAUSED) continue;

            auto start = std::chrono::steady_clock::now();

            // Fecth the next instruction. The instruction has 4 nibbles.
            uint16_t instr = PACK16(memory.Read(cpu.programCounter),
                                    memory.Read(cpu.programCounter + 1));
            cpu.programCounter += 2;
            // Decode the instruction

            auto opcode = FIRST_NIBBLE(instr);

            switch (opcode) {
                case 0x01: {
                    // JUMP
                    break;
                }
                case 0x6: {
                    // SET Register
                    break;
                }
                case 0x7: {
                    // ADD Value to Register
                    break;
                }
                case 0xA: {
                    // SET Index Register I (ANNN)
                    cpu.indexRegister = TWELVE(instr);
                    break;
                }
                case 0xD: {
                    // DXYN display/draw
                    break;
                }
                default: {
                    std::cerr << "Not implemented yet: " << opcode << "\n";
                    std::exit(EXIT_FAILURE);
                }
            }

            auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start);

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
    vec.insert(vec.begin(), std::istream_iterator<uint8_t>(file),
               std::istream_iterator<uint8_t>());

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