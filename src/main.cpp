#include <SDL2/SDL.h>
#include <_types/_uint16_t.h>
#include <_types/_uint8_t.h>

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
#include <cstdlib>

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
constexpr std::size_t FONT_ADDRESS_OFFSET = 0x50;
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
    static constexpr uint32_t DEFAULT_SCALE_FACTOR = 20;

    uint32_t scaleFactor{DEFAULT_SCALE_FACTOR};
    bool useScanline{true};
    chip8::graphics::colors::Color scanline{0x0f, 0x0f, 0x0f, 0xff};
    chip8::graphics::colors::Color fgColor = chip8::graphics::colors::GREEN;
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

    inline bool ReadPixel(std::size_t x, std::size_t y) { 
#ifdef DEBUG
    std::fprintf(stdout, "[info] :: reading x=%ld,y=%ld\n", x, y);
#endif
        assert(0 <= x && x < DISPLAY_WIDTH);
        assert(0 <= y && y < DISPLAY_HEIGHT);
        return data[DISPLAY_WIDTH * y + x]; 
    }

    void DrawAll(bool value) {
        for (std::size_t x = 0; x < DISPLAY_WIDTH; x++) {
            for (std::size_t y = 0; y < DISPLAY_HEIGHT; y++) {
                Draw(x, y, value);
            }
        }
    }

    void Draw(std::size_t x, std::size_t y, bool value) { 
#ifdef DEBUG
    std::fprintf(stdout, "[info] :: drawing at x=%ld,y=%ld on=%d\n", x, y, value);
#endif
        assert(0 <= x && x < DISPLAY_WIDTH);
        assert(0 <= y && y < DISPLAY_HEIGHT);
        data[DISPLAY_WIDTH * y + x] = value; 
    }

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
    static constexpr std::size_t STARTING_PC = 0x200;
    static constexpr std::size_t NUMBER_OF_V_REGISTERS = 0x10;
    static constexpr std::size_t STACK_SIZE = 0x10;

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
    std::array<uint8_t, NUMBER_OF_V_REGISTERS> V{};
    /// The stack is an aray of 16bit 16 value (that means up to 16 subroutines nested).
    std::array<uint16_t, STACK_SIZE> stack{};
};

class Memory {
    static constexpr std::size_t MEMORY_SIZE = 1 << 12;  /// 4KiB
    std::array<uint8_t, MEMORY_SIZE> data{};

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

class Keyboard {
    static constexpr std::size_t KEYBOARD_SIZE = 16;
    std::array<bool, KEYBOARD_SIZE> keyboard{};
public:
    void PressKey(std::size_t key) {
        assert(0 <= key && key < KEYBOARD_SIZE);
        keyboard[key] = true;
    }
    void ReleaseKey(std::size_t key) {
        assert(0 <= key && key < KEYBOARD_SIZE);
        keyboard[key] = false;
    }
    bool isKeyPressed(std::size_t key) {
        assert(0 <= key && key < KEYBOARD_SIZE);
        return keyboard[key];
    }
};

}  // namespace system

class Emulator {
    enum class Status { RUNNING, PAUSED, WAITING_FOR_KEY, STOPPED };

    Config config{};

    bool shouldRedraw {false};
    std::optional<uint8_t> destinationKeyRegister {std::nullopt}; // The KeyPad is hexdecimal 0-F

    chip8::system::Cpu cpu;
    chip8::system::Memory memory;
    chip8::system::Keyboard keyboard;
    chip8::display::Screen screen{config};

    Status currentStatuts{Status::RUNNING};

    void Jump(uint16_t instr, bool hasOffset = false) {
        auto offset = (hasOffset) ? cpu.V[0] : 0;
        cpu.PC = TWELVE(instr) + offset;
#ifdef DEBUG
        std::fprintf(stdout, "[info] :: jumping to address '0x%x'\n", TWELVE(instr) + offset);
#endif
    }

    void LoadIntoV(uint16_t instr) {
        // 6xkk - LD Vx, byte
        auto x = SECOND_NIBBLE(instr);
        assert(0 <= x && x < 0xf0);
        auto byte = LSB(instr);
        cpu.V[x] = byte;
    }

    void SetIndexRegister(uint16_t instr) {
        cpu.I = TWELVE(instr);  // SET Index Register I (0xANNN)
    }

    void ClearScreen(uint16_t) {
        screen.DrawAll(false);
        shouldRedraw = true;
#ifdef DEBUG
        std::fprintf(stdout, "[info] :: cleaning screen...\n");
#endif
    }

    void Call(uint16_t instr) {
        auto address = TWELVE(instr);
        cpu.SP++;
        cpu.stack[cpu.SP] = cpu.PC;
        cpu.PC = address;
#ifdef DEBUG
        std::cerr << "[info] :: calling routine at: 0x" << std::hex << address << std::endl;
#endif
    }

    void ReturnFromRoutine(uint16_t) {
        // Return from Subroutine
        cpu.PC = cpu.stack[cpu.SP];
#ifdef DEBUG
        std::fprintf(stdout, "[info] :: returning to '0x%lx'\n", cpu.PC);
#endif
        cpu.SP--;
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
                // 8xy4 - ADD Vx, Vy
                // Set Vx = Vx + Vy, set VF = carry.
                uint8_t vx = cpu.V[x], vy = cpu.V[y];
                if (static_cast<uint16_t>(vx) + static_cast<uint16_t>(vy) > 0xff) {
                    cpu.V[0xf] = 1;
                } else {
                    cpu.V[0xf] = 0;
                }
                cpu.V[x] = vx + vy;
                break;
            }
            case 0x5: {
                // 8xy5 - SUB Vx, Vy
                // Set Vx = Vx - Vy, set VF = NOT borrow.
                uint8_t vx = cpu.V[x], vy = cpu.V[y];
                if (vx > vy) {
                    cpu.V[0xf] = 1;
                } else {
                    cpu.V[0xf] = 0;
                }
                cpu.V[x] = vx - vy;
                break;
            }
            case 0x6: {
                // 8xy6 - SHR Vx {, Vy}
                // Set Vx = Vx SHR 1.
                uint8_t vx = cpu.V[x];
                if (vx & 1) {
                    cpu.V[0xf] = 1;
                } else {
                    cpu.V[0xf] = 0;
                }
                cpu.V[x] = vx >> 1;
                break;
            }
            case 0x7: {
                // 8xy7 - SUBN Vx, Vy
                // Set Vx = Vy - Vx, set VF = NOT borrow.
                uint8_t vx = cpu.V[x], vy = cpu.V[y];
                if (vy > vx) {
                    cpu.V[0xf] = 1;
                } else {
                    cpu.V[0xf] = 0;
                }
                cpu.V[x] = vy - vx;
                break;
            }
            case 0xE: {
                // 8xyE - SHL Vx {, Vy}
                // Set Vx = Vx SHL 1.
                uint8_t vx = cpu.V[x];
                if ((vx >> 0x7) & 1) {
                    cpu.V[0xf] = 1;
                } else {
                    cpu.V[0xf] = 0;
                }
                cpu.V[x] = vx << 1;
                break;
            }
            default: {
                std::cerr << "[error] :: unimplemented operator\n";
                std::exit(-1);
            }
        }
    }

    void Add(uint16_t instr) {
        auto reg = SECOND_NIBBLE(instr);
        assert(0x0 <= reg && reg <= 0xf);
        cpu.V[reg] += LSB(instr);
    }

    void SkipEqual(uint16_t instr, bool compareRegister) {
        // 4xkk/5xy0
        auto reg = SECOND_NIBBLE(instr);
        auto value = (compareRegister) ? (cpu.V[THIRD_NIBBLE(instr)]) : LSB(instr);
        if (cpu.V[reg] == value) {
            cpu.PC += 2;
        }
    }

    void SkipNotEqual(uint16_t instr, bool compareRegister) {
        // 4xkk - SNE Vx, byte  (compareRegister=false)
        // 9xy0 - SNE Vx, Vy    (compareRegister=true)
        auto x = SECOND_NIBBLE(instr);
        auto value = (compareRegister) ? (cpu.V[THIRD_NIBBLE(instr)]) : LSB(instr);
        if (cpu.V[x] != value) {
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
                    screen.Draw(x0, y0, 0);
                    cpu.V[CARRY_FLAG] = 0x1;
                }
                else {
                    screen.Draw(x0, y0, pixel ^ spriteBit);
                }

                if (++x0 >= chip8::display::DISPLAY_WIDTH) break;
            }

            if (++y0 >= chip8::display::DISPLAY_HEIGHT) break;
        }
        shouldRedraw = true;
    }

    void Random(uint16_t instr) {
        auto x = SECOND_NIBBLE(instr);
        auto lsb = LSB(instr);
        auto rnd = std::rand() % 0x100;
        cpu.V[x] = lsb & rnd;
    }

    void FDispatcher(uint16_t instr) {
        switch (LSB(instr)) {
            case 0x07: {
                // Set Vx = delay timer value.
                auto x = SECOND_NIBBLE(instr);
                cpu.V[x] = cpu.delayTimer;
                break;
            }
            case 0x0A: {
                // FX0A
                destinationKeyRegister = SECOND_NIBBLE(instr);
                currentStatuts = Status::WAITING_FOR_KEY;
                break;
            }
            case 0x15: {
                // Fx15 - Set delay timer
                cpu.delayTimer = cpu.V[SECOND_NIBBLE(instr)];
                break;
            }
            case 0x18: {
                // Fx18 - Set sound timer
                cpu.soundTimer = cpu.V[SECOND_NIBBLE(instr)];
                break;
            }
            case 0x1e: {
                // Fx1E - ADD I, Vx
                cpu.I += cpu.V[SECOND_NIBBLE(instr)];
                break;
            }
            case 0x29: {
                // Fx29 - LD F, Vx
                // Set I = location of sprite for digit Vx.
                uint8_t vx = cpu.V[SECOND_NIBBLE(instr)];
                cpu.I = static_cast<uint16_t>(vx) * 5 + graphics::fonts::FONT_ADDRESS_OFFSET;
                break;
            }
            case 0x33: {
                // Fx33 - LD B, Vx
                // Store BCD representation of Vx in memory locations I, I+1, and I+2.
                uint8_t vx = cpu.V[SECOND_NIBBLE(instr)];
                memory.Write8(cpu.I, static_cast<uint8_t>(static_cast<uint16_t>((vx % 1000) / 100)));
                memory.Write8(cpu.I + 1, (vx % 100) / 10);
                memory.Write8(cpu.I + 2, vx % 10);
                break;
            }
            case 0x55: {
                // Fx55 - LD [I], Vx
                for (std::size_t i = 0; i < SECOND_NIBBLE(instr); i++) {
                    memory.Write8(i + cpu.I, cpu.V[i]);
                }
                break;
            }
            case 0x65: {
                // Fx65 - LD Vx, [I]
                for (std::size_t i = 0; i < SECOND_NIBBLE(instr); i++) {
                    cpu.V[i] = memory.Read8(i + cpu.I);
                }
                break;
            }
            default: {
                std::cerr << "[error] :: Not implemented yet: 0x" << std::hex << instr << ".\n";
                std::exit(-1);
            }
        }
    }

    void SkipIfKey(uint16_t instr) {

        // Ex9E - SKP Vx:  Skip next instruction if key with the value of Vx is pressed.
        // ExA1 - SKNP Vx: Skip next instruction if key with the value of Vx is not pressed.

        uint8_t subop = LSB(instr);
        bool shouldSkip = false;

        uint8_t vx = this->cpu.V[SECOND_NIBBLE(instr)];

        if (subop == 0x9E) {
            shouldSkip = keyboard.isKeyPressed(vx);
        } else if (subop == 0xA1) {
            shouldSkip = !keyboard.isKeyPressed(vx);
        }

        if (shouldSkip) {
            cpu.PC += 2;
        }
    }

   public:
    explicit Emulator() {
        std::srand(std::time(nullptr));
    }

    void LoadFont(const chip8::graphics::fonts::Font font) { memory.WriteBytes(font, graphics::fonts::FONT_ADDRESS_OFFSET); }

    void LoadRom(const std::vector<uint8_t>&& rom) {
        memory.WriteBytes(std::move(rom), chip8::system::Cpu::STARTING_PC);
    }

    void Run() {

        while (currentStatuts != Status::STOPPED) {

            if (cpu.delayTimer > 0) {
                cpu.delayTimer--;
            }
            if (cpu.soundTimer > 0) {
                cpu.soundTimer--;
            }

            screen.PollEvent([this](const SDL_Event& event) {
                if (event.type == SDL_QUIT) {
                    std::exit(EXIT_FAILURE);
                }
                if (event.type == SDL_KEYUP) {
                    uint8_t releasedKey = 0;
                    auto key = event.key.keysym.sym;
                    if (key >= SDLK_0 && key <= SDLK_9) {
                        releasedKey = (key - '0');
                        assert(0 <= releasedKey && releasedKey <= 0xf);
                        keyboard.ReleaseKey(releasedKey);
#ifdef DEBUG
                        std::fprintf(stdout, "[info] :: key released index=%d\n", releasedKey);
#endif
                    }
                    if (key >= SDLK_a && key <= SDLK_f) {
                        releasedKey = (key - 'a') + 0xa;
                        assert(0 <= releasedKey && releasedKey <= 0xf);
                        keyboard.ReleaseKey(releasedKey);
#ifdef DEBUG
                        std::fprintf(stdout, "[info] :: key released index=%d\n", releasedKey);
#endif
                    }
                }
                if (event.type == SDL_KEYDOWN) {

                    uint8_t pressedKey = 0;
                    auto key = event.key.keysym.sym;

                    // If Q or Escape is pressed quit the emulator.
                    if (key == SDLK_ESCAPE || key == SDLK_q) {
                        std::exit(EXIT_FAILURE);
                    }
                    
                    // 0 to 9
                    if (key >= SDLK_0 && key <= SDLK_9) {
                        pressedKey = (key - '0');
                        keyboard.PressKey(pressedKey);
#ifdef DEBUG
                        std::fprintf(stdout, "[info] :: key pressed index=%d\n", pressedKey);
#endif
                    }
                    if (key >= SDLK_a && key <= SDLK_f) {
                        pressedKey = (key - 'a') + 0xa;
                        keyboard.PressKey(pressedKey);
#ifdef DEBUG
                        std::fprintf(stdout, "[info] :: key pressed index=%d\n", pressedKey);
#endif
                    }
#ifdef DEBUG
                    std::cerr << "[info] :: pressed key number: " << static_cast<char>(key) << "\n";
#endif

                    if (this->destinationKeyRegister.has_value()) {
                        auto x = this->destinationKeyRegister.value();
                        assert(0 <= x && x <= 0xf);
                        assert(0 <= pressedKey && pressedKey <= 0xf);
                        this->cpu.V[x] = pressedKey;
                        this->destinationKeyRegister = std::nullopt;
                        this->currentStatuts = Status::RUNNING;
                    }
                }
            });

            if (currentStatuts != Status::RUNNING) {
                // The timer needs to be synchronized
                screen.Delay();
                continue;
            }

            auto start = std::chrono::steady_clock::now();

            // Fecth the next instruction. The instruction has 4 nibbles.
            uint16_t instr = memory.Read16(cpu.PC);
            cpu.PC += 2;

#if DEBUG
            std::fprintf(stdout, "[info] :: executing instruction '0x%x'\n", instr);
#endif

            // Decode the instruction
            uint8_t opcode = FIRST_NIBBLE(instr);
            switch (opcode) {
                case 0x0: {
                    // Clear Screen
                    if (instr == 0x00E0) {
                        ClearScreen(instr);
                    }
                    else if (instr == 0x00EE) {
                        ReturnFromRoutine(instr);
                    }
                    else {
                        std::cerr << "[error] illegal instruction...\n";
                        std::exit(EXIT_FAILURE);
                    }
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
                case 0xC: {
                    Random(instr);
                    break;
                }
                case 0xD: {
                    DrawPixels(instr);
                    break;
                }
                case 0xE: { 
                    SkipIfKey(instr);
                    break;
                }
                case 0xF: {
                    FDispatcher(instr);
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
            if (shouldRedraw) {
                screen.Update();
                shouldRedraw = false;
            }
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