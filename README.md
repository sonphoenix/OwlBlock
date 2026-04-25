# GBA Emulator

A Game Boy Advance emulator written in C++.

## Current Status
- ARM7TDMI CPU core (ARM mode)
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

## TODO
- Thumb instruction set
- PPU
- DMA
- Timers
- Interrupts
- APU

## Building
Open `Owl BLock.sln` in Visual Studio 2022 and build.

## BIOS
You need to provide your own GBA BIOS file named `gba_bios.bin`.
Place it in the project root directory.