# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## About DOSBox-X

DOSBox-X is a cross-platform DOS emulator that goes beyond gaming to support DOS applications, Windows 3.x/9x/ME, and specialized systems like PC-98. It's a fork of DOSBox with enhanced accuracy, features, and hardware emulation.

## Common Development Commands

### Building the Project

**Linux/BSD (SDL1):**
```bash
./build-debug
sudo make install
```

**Linux/BSD (SDL2):**
```bash
./build-debug-sdl2
sudo make install
```

**macOS:**
```bash
# Install dependencies first
brew install autoconf automake nasm glfw glew fluid-synth libslirp pkg-config

# Build (SDL1 or SDL2)
./build-macos
# or
./build-macos-sdl2

# Create App Bundle
make dosbox-x.app
```

**MinGW (Windows):**
```bash
# Install required libraries first (see BUILD.md for specific packages)
./build-mingw
# or
./build-mingw-sdl2
```

### Testing and Development

**Run the built executable:**
```bash
# From build directory
./src/dosbox-x

# With debug configuration
./src/dosbox-x -defaultconf -silent
```

**Create config file:**
```bash
./src/dosbox-x -printconf > dosbox-x.conf
```

**Enable debugging:**
- Use `--enable-debug` during configure
- Set `output=surface` in config for debugger compatibility
- Press Alt+Pause in SDL window to enter debugger

### Clean and Rebuild

```bash
# Clean build files
make clean

# Full clean including autotools
./cleantree

# Regenerate build system
./autogen.sh
```

## High-Level Code Architecture

### Core Components

**CPU Emulation (`src/cpu/`):**
- Multiple CPU cores: normal, full, dynamic recompiler, prefetch variants
- Entry point: `src/cpu/cpu.cpp`
- Supports 8086 through Pentium III instruction sets
- Different cores optimized for speed vs accuracy

**Hardware Emulation (`src/hardware/`):**
- VGA/SVGA graphics (`vga*.cpp`) with vendor-specific implementations
- Sound cards: Sound Blaster (`sblaster.cpp`), AdLib (`adlib.cpp`), GUS (`gus.cpp`)
- Input devices: keyboard, mouse, joystick
- Storage: IDE, floppy drives
- Specialized: PC-98 hardware (`pc98*.cpp`)

**DOS Kernel (`src/dos/`):**
- File system emulation (`drive_*.cpp`)
- Memory management (`dos_memory.cpp`)
- Program execution (`dos_execute.cpp`)
- Device drivers and MSCDEX support

**BIOS/Interrupts (`src/ints/`):**
- INT 10h: Video BIOS (`int10*.cpp`)
- INT 13h: Disk services (`bios_disk.cpp`)
- INT 21h: DOS functions (handled in DOS layer)
- Mouse driver (`mouse.cpp`)

**User Interface (`src/gui/`):**
- SDL integration (`sdlmain.cpp`) - main event loop
- Rendering system (`render.cpp`) with multiple output backends
- Menu system (`menu*.cpp`)
- Video output scaling and shaders

### Key Architecture Patterns

**Layered Design:**
1. Application Layer (GUI, menus, configuration)
2. DOS Layer (kernel, file system, program execution) 
3. BIOS Layer (interrupt handlers, hardware abstraction)
4. Hardware Layer (device emulation)
5. CPU Layer (instruction execution, memory management)

**Hardware Integration:**
- I/O port registration system for hardware devices
- Timer-based event scheduling for accurate hardware timing
- Interrupt system for device communication
- Memory mapping with banking/paging support

**Cross-Platform Support:**
- Platform abstraction in `src/misc/` and `src/platform/`
- Conditional compilation for OS-specific features
- Runtime capability detection

### Important Files and Entry Points

**Main Entry Points:**
- `src/dosbox.cpp` - Application initialization and main()
- `src/gui/sdlmain.cpp` - SDL event loop and window management
- `include/dosbox.h` - Core definitions and global state

**Configuration System:**
- `src/misc/setup.cpp` - Configuration file parsing
- `dosbox-x.reference.conf` - Complete configuration template
- `dosbox-x.reference.full.conf` - Extended configuration with all options

**Build System:**
- `configure.ac` - Autotools configuration with feature detection
- `Makefile.am` - Build rules, especially macOS app bundle creation
- `build-*` scripts - Platform-specific build helpers

### Special Features

**PC-98 Emulation:**
- Complete Japanese PC-98 system support in `src/hardware/pc98*.cpp`
- Separate graphics system with different character sets
- Enhanced for running PC-98 games and applications

**Save States:**
- Full system state preservation in `src/misc/savestates.cpp`
- Supports up to 100 save slots

**Built-in Programs:**
- `src/builtin/` contains DOS utilities compiled into the executable
- Provides essential DOS commands without external files

**Multiple Output Backends:**
- `src/output/` - OpenGL, Direct3D, surface rendering
- Hardware-accelerated scaling and shader support

### Development Tips

**Debugging:**
- Use debug builds (`./build-debug*`) for development
- Built-in debugger accessible with Alt+Pause
- Extensive logging system controlled by config file

**Testing:**
- Test suite in `tests/` directory
- Platform-specific behavior testing important
- Real DOS software compatibility is primary goal

**Adding Hardware:**
- Follow existing patterns in `src/hardware/`
- Register I/O ports in device init functions
- Implement timer callbacks for periodic operations
- Add configuration options in setup system

**Cross-Platform Considerations:**
- Test on multiple platforms before submitting changes
- Use abstraction layers for OS-specific functionality
- Be aware of endianness and 32/64-bit differences

### Notable Dependencies

**Required Libraries:**
- SDL 1.2.x or SDL 2.x (heavily modified in-tree version)
- Autotools for building (autoconf, automake)

**Optional Libraries:**
- FluidSynth (MIDI synthesis)
- libslirp (network emulation) 
- libpcap (network capture)
- FreeType (TrueType font rendering)
- Various audio codecs (FLAC, MP3, Opus)

**Included Libraries:**
- MT-32 emulation (`src/libs/mt32/`)
- xBRZ scaling (`src/libs/xBRZ/`)
- Audio decoders (`src/libs/decoders/`)
- ZMBV video codec (`src/libs/zmbv/`)

This architecture enables DOSBox-X to provide highly accurate emulation while maintaining good performance and extensibility across multiple platforms.