# Owl Block — GBA Emulator

A Game Boy Advance emulator written in C++ from scratch, built as a learning project in hardware-accurate emulation.

## Current Status

### CPU — ARM7TDMI
- Full ARM and THUMB instruction sets
- All data processing instructions (AND, EOR, SUB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN)
- Branch / BL / BX / BLX
- LDR/STR / LDRH/STRH / LDRSB/LDRSH / LDRD/STRD
- LDM/STM with all addressing modes
- MUL/MLA/UMULL/UMLAL/SMULL/SMLAL
- MRS/MSR (CPSR/SPSR access)
- SWP/SWPB
- All CPU modes with correct banked register switching (USR/FIQ/IRQ/SVC/ABT/UND/SYS)
- SWI handling — HLE for SWI 4 (IntrWait) and SWI 5 (VBlankIntrWait)
- Passes standard ARM/THUMB test suites
- Soft reset (F2): reinitializes registers and re-enters the ROM without reloading BIOS/ROM from disk

### Bus / Memory
- Full GBA memory map: BIOS, EWRAM, IWRAM, VRAM, OAM, PRAM, ROM, I/O
- BIOS protection and latch emulation
- IWRAM mirroring across full 0x03000000–0x03FFFFFF range
- ROM mirrors at 0x08000000, 0x0A000000, 0x0C000000
- VRAM byte-write rules (ignored in tiled modes, halfword-duplicated in bitmap/OBJ area)
- Palette RAM byte-write duplication

### Scheduler
- Event-driven scheduler replacing per-cycle polling for HBlank, scanline advance, and timer overflow
- Components register a target cycle instead of being checked every cycle; the main loop jumps straight to the next due event
- Timer counter reads are computed live from the scheduler's pending overflow event rather than ticked in a loop

### DMA
- All 4 channels (DMA0–DMA3)
- Immediate, VBlank, HBlank, and Special (FIFO) timing modes
- Word and halfword transfer sizes
- Source/destination increment/decrement/fixed/reload modes
- Repeat mode with correct reload behavior
- Hardware zero-count maximums (0x4000 / 0x10000)
- DMA IRQ on completion

### Interrupts
- IME / IE / IF registers
- IRQ dispatch via per-cycle checkIRQ()
- VBlank, HBlank, VCount, Timer 0–3, DMA 0–3, Keypad interrupts
- CPU halt/unhalt via HALTCNT (0x04000301)

### Timers
- All 4 timers (TM0–TM3)
- Prescaler modes (1/64/256/1024 cycles)
- Cascade mode (timer N triggered by overflow of timer N-1)
- Shadow reload registers (reload latch separate from running counter)
- Timer IRQ on overflow
- Scheduler-driven overflow with live counter snapshotting on read
- APU FIFO driven by timer overflow

### PPU
- Background modes 0, 1, 2 (tiled)
- Background modes 3, 4, 5 (bitmap)
- 4bpp and 8bpp tile rendering
- Sprites (OBJ layer): regular and affine, 4bpp/8bpp, all sizes
- Affine backgrounds with per-scanline PB/PD increment
- Window system (WIN0, WIN1, WINOBJ, WINOUT)
- BG/OBJ priority sorting with tie-breaking by index
- Correct scanline-by-scanline rendering

### APU
- PSG Channel 1: square wave with frequency sweep and volume envelope
- PSG Channel 2: square wave with volume envelope
- PSG Channel 3: wave channel, 32-nibble wave RAM playback, 100%/50%/25%/mute volume shifting, 256-step length counter
- PSG Channel 4: noise channel, 15-bit/7-bit (short mode) LFSR, divisor + clock-shift period, envelope
- DMA FIFO channels A and B: timer-driven pop, DMA refill
- Frame sequencer at 512Hz (length counters, sweep, envelope)
- Master volume (NR50) and per-channel left/right panning (NR51)
- Raylib AudioStream output (32768Hz, stereo, 16-bit)

### Save System
- Auto-detection from ROM header markers (EEPROM_V, SRAM_V, FLASH_V, FLASH512_V, FLASH1M_V)
- Per-game save files: each ROM gets its own `.sav` in `saves/`, named after the ROM file
- SRAM (32KB)
- EEPROM: full bit-serial state machine; 6-bit vs 14-bit addressing is detected live from each DMA transfer's word count, not guessed from ROM size
- Flash 64KB / 128KB: detected, save data allocated (full command emulation pending)
- Save loaded on ROM load, written to disk on clean exit

### Debugger (built-in)
- Raylib-based debug panel alongside the game view, toggleable on/off
- GFX tab: palette viewer (BG + OBJ), tile viewer, tile map viewer
- Memory tab: hex viewer with goto, presets, ASCII panel, byte detail status bar
- CPU tab: registers, CPSR flags, per-bit IRQ table (IE/IF/FIRED), DISPSTAT IRQ enables, key memory watches, PC trace, inline ARM/THUMB disassembly
- Pause/resume (P), single-step (O), turbo step (hold O / Shift+O for 1000x)
- Breakpoint system: set address (B), run-to-breakpoint (R), cancel (ESC)
- ARM/THUMB disassembler covering the full instruction set

## Building

This project uses CMake. Dependencies are fetched automatically on first configure (Dear ImGui docking branch, rlImGui) or linked from prebuilt binaries already in the repo (raylib, in `lib/`).

**In Visual Studio 2022:**
- **File → Open → Folder...** and select the project root (the folder containing `CMakeLists.txt`)
- VS auto-detects the CMake project and configures it — check the Output window (CMake pane) for progress
- Pick a configuration (`x64-Debug` or `x64-Release`) from the top toolbar, select `OwlBlock.exe` as the run target, and build (`Ctrl+Shift+B`) or run (▶)

**From a terminal (any OS):**
```
cmake -B build
cmake --build build --config Release
```

Dependencies:
- raylib — rendering and audio (prebuilt, in `lib/`)
- Dear ImGui (docking branch) + rlImGui — fetched automatically via CMake
- nlohmann/json — included in `include/vendor/nlohmann/`

## Setup

**BIOS:** Place your GBA BIOS file as `bios/gba_bios.bin`. The emulator will not run without it. You must dump this from your own hardware.

**ROM:** Place your `.gba` ROM file in the `roms/` folder, or adjust the path in `main.cpp`.

**Saves:** Written automatically to `saves/<rom-name>.sav`, created on first load of a given ROM.

## Controls

| Key | GBA Button |
|---|---|
| X | A |
| Z | B |
| S | L |
| A | R |
| Enter | Start |
| Backspace | Select |
| Arrow keys | D-Pad |

## Debugger Controls

| Key | Action |
|---|---|
| P | Pause / Resume |
| F1 | Toggle debug panel / window layout |
| F2 | Soft reset (reinitialize CPU/PPU/APU/DMA/timers, keep ROM and save data) |
| O | Single step |
| Hold O | Fast step |
| Shift+O | Turbo step (1000x) |
| B | Set breakpoint address |
| R | Run to breakpoint |
| ESC | Cancel run-to-breakpoint |
| G (memory tab) | Go to address |
