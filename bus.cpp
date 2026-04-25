#include "Bus.h"

uint8_t Bus::read8(uint32_t address) {
    if (address < 0x4000) {
        return bios[address];
    }
    else if (address >= 0x02000000 && address < 0x02040000) {
        return ewram[address - 0x02000000];
    }
    else if (address >= 0x03000000 && address < 0x03008000) {
        return iwram[address - 0x03000000];
    }
    else if (address >= 0x04000000 && address < 0x04000400) {
        return io[address - 0x04000000];
    }
    else if (address >= 0x05000000 && address < 0x05000400) {
        return pram[address - 0x05000000];
    }
    else if (address >= 0x06000000 && address < 0x06018000) {
        return vram[address - 0x06000000];
    }
    else if (address >= 0x07000000 && address < 0x07000400) {
        return oam[address - 0x07000000];
    }
    else if (address >= 0x08000000 && address < 0x0A000000) {
        return rom[address - 0x08000000];
    }
    return 0;
}

uint16_t Bus::read16(uint32_t address) {
    return (uint16_t)read8(address) | ((uint16_t)read8(address + 1) << 8);
}

uint32_t Bus::read32(uint32_t address) {
    return (uint32_t)read8(address)
         | ((uint32_t)read8(address + 1) << 8)
         | ((uint32_t)read8(address + 2) << 16)
         | ((uint32_t)read8(address + 3) << 24);
}

void Bus::write8(uint32_t address, uint8_t value) {
    if (address >= 0x02000000 && address < 0x02040000) {
        ewram[address - 0x02000000] = value;
    }
    else if (address >= 0x03000000 && address < 0x03008000) {
        iwram[address - 0x03000000] = value;
    }
    else if (address >= 0x04000000 && address < 0x04000400) {
        io[address - 0x04000000] = value;
    }
    else if (address >= 0x05000000 && address < 0x05000400) {
        pram[address - 0x05000000] = value;
    }
    else if (address >= 0x06000000 && address < 0x06018000) {
        vram[address - 0x06000000] = value;
    }
    else if (address >= 0x07000000 && address < 0x07000400) {
        oam[address - 0x07000000] = value;
    }
}

void Bus::write16(uint32_t address, uint16_t value) {
    write8(address, value & 0xFF);
    write8(address + 1, (value >> 8) & 0xFF);
}

void Bus::write32(uint32_t address, uint32_t value) {
    write8(address, value & 0xFF);
    write8(address + 1, (value >> 8) & 0xFF);
    write8(address + 2, (value >> 16) & 0xFF);
    write8(address + 3, (value >> 24) & 0xFF);
}

void Bus::loadBIOS(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cout << "failed to load BIOS\n";
        return;
    }
    f.read((char*)bios.data(), 0x4000);
}

void Bus::loadROM(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cout << "failed to load ROM\n";
        return;
    }
    f.read((char*)rom.data(), 0x2000000);
}