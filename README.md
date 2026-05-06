# GBA Emulator (Owl Block)
A Game Boy Advance emulator written in C++.

## Current Status
- ARM7TDMI CPU core (ARM/THUMB mode)
  - Data processing (AND, ADD, SUB, etc)
  - Branch / BL / BX
  - LDR/STR / LDRH/STRH / LDRSB/LDRSH
  - LDM/STM
  - SWP
  - MUL/MLA/MULL/MLAL
  - MRS/MSR
  - CPU modes + banked registers
  - SWI
- Bus with full GBA memory map
- BIOS protection + BIOS latch
- DMA (all 4 channels, immediate/VBlank/HBlank)
- Interrupts (IME/IE/IF, IRQ handling)
- PPU bitmap modes
  - Mode 3 (240x160 direct color)
  - Mode 4 (240x160 paletted, double buffered)
  - Mode 5 (160x128 direct color, double buffered)

## TODO
- PPU tile modes (Mode 0/1/2)
- Sprites (OBJ layer)
- Timers
- APU
- Affine backgrounds
- Window system

## Building
Open `Owl BLock.sln` in Visual Studio 2022 and build.

## BIOS
You need to provide your own GBA BIOS file named `gba_bios.bin` placed in the project root directory.