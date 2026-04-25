#pragma once
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
struct Bus {
    std::vector<uint8_t> bios = std::vector<uint8_t>(0x4000, 0);
    std::vector<uint8_t> ewram = std::vector<uint8_t>(0x40000, 0);
    std::vector<uint8_t> iwram = std::vector<uint8_t>(0x8000, 0);
    std::vector<uint8_t> pram = std::vector<uint8_t>(0x400, 0);
    std::vector<uint8_t> vram = std::vector<uint8_t>(0x18000, 0);
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x2000000, 0);
    std::vector<uint8_t> io = std::vector<uint8_t>(0x400, 0);
    std::vector<uint8_t> oam = std::vector<uint8_t>(0x400, 0);

    uint8_t  read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    void     write8(uint32_t address, uint8_t value);
    void     write16(uint32_t address, uint16_t value);
    void     write32(uint32_t address, uint32_t value);
    void     loadBIOS(const char* path);
    void     loadROM(const char* path);
};