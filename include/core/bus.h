#pragma once
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include<string>
#include "save/SaveController.h"
#include "timer/TimerController.h"
#include "dma/DMAController.h"
#include "scheduler/Scheduler.h"

struct CPU;
struct PPU;
class APU;

struct Bus {
    CPU* cpuptr = nullptr;
    PPU* ppuptr = nullptr;
    APU* apuptr = nullptr;

    //memory data register 
    uint32_t mdr = 0;
    uint32_t bios_latch = 0;
    uint8_t postflg = 0;
    uint8_t vcount = 0;      // current scanline (0-227)
    uint16_t prev_irq_signal = 0; // Member variable to track IRQ state
    int frameCount = 0;
    uint16_t win0h_scanline[160] = {};
    uint16_t win0v_scanline[160] = {};
    std::vector<uint8_t> bios;
    std::vector<uint8_t> ewram;
    std::vector<uint8_t> iwram;
    std::vector<uint8_t> pram;
    std::vector<uint8_t> vram;
    std::vector<uint8_t> rom;
    std::vector<uint8_t> io;
    std::vector<uint8_t> oam;
    std::string currentSavePath;
    SaveController save;
    TimerController timers;
    DMAController dmaController;
    Scheduler scheduler;

    Bus();
    uint8_t  read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    void init();                    // call once after construction
    void advance(uint32_t cycles);  // new main entry point, replaces per-cycle tick()
    void fireHBlank();
    void fireScanlineAdvance();
    void checkIRQ();
    void setCPU(CPU* _cpu);
    void updateBiosLatch(uint32_t inst);
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);
    void loadBIOS(const char* path);
    void loadROM(const char* path);
    void setKeyState(int bit, bool pressed);
    void onVBlank();
    void onHBlank();
};