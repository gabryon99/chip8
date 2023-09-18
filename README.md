## Chip8(++)

This is an emulator written in modern **C++17**. It is somewhat buggy but you can load roms
and draw sprites on the screen, and play games!

<img width="1392" alt="chip8 emulator" src="https://github.com/gabryon99/chip8/assets/14114916/25f3920e-b029-48ab-9e37-1a1b48418181">

### Test Roms

- [x] IBM Logo
- [x] BPM Viewer
- [x] Chip8 Logo
- [x] KeyPad Test
- [x] Zero Demo

### Next Features

- [ ] Game Tuner: since CHIP8 games do not have any header file, maybe we can use a _checksum_.
- [ ] Creating a debugger with breakpoints.
- [ ] Showing CPU status in a separate window.
- [x] Implementing missing arithmetic instructions with carry.
- [ ] Parsing CLI options (scale size, colors, and so on...)
- [x] Implement timers
- [x] Implement sound
- [ ] Separate namespace in different files.
- [ ] Creating a CMake file.
- [ ] Run on other platforms (e.g., Linux).
- [ ] Experiment other emulation techniques for the main fetch-decode-execute loop: threaded-code, just-in-time compilation...

