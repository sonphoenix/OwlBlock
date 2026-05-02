#include "Bus.h"
#include <algorithm>  // for std::min
#include <cstring>

// -------------------------------------------------------------------
// Constructor – initialise VCOUNT and step counter
// -------------------------------------------------------------------
Bus::Bus() : postflg(0), vcount(0), step_counter(0) {
    // vectors are already zero-initialised thanks to their declarations
}

// -------------------------------------------------------------------
// Read a byte from the GBA address space
// -------------------------------------------------------------------
uint8_t Bus::read8(uint32_t address) {
    uint8_t result = 0;

    if (address < 0x4000) {
        result = bios[address];
    }
    else if (address >= 0x02000000 && address < 0x02040000) {
        result = ewram[address - 0x02000000];
    }
    else if (address >= 0x03000000 && address < 0x03008000) {
        result = iwram[address - 0x03000000];
    }
    else if (address >= 0x04000000 && address < 0x04000400) {
        // Special handling for POSTFLG and VCOUNT
        if (address == 0x04000300) {
            return postflg;
        }
        if (address == 0x04000006) {
            return vcount;                 // VCOUNT – current scanline
        }
        result = io[address - 0x04000000];
    }
    else if (address >= 0x05000000 && address < 0x05000400) {
        result = pram[address - 0x05000000];
    }
    else if (address >= 0x06000000 && address < 0x06018000) {
        result = vram[address - 0x06000000];
    }
    else if (address >= 0x07000000 && address < 0x07000400) {
        result = oam[address - 0x07000000];
    }
    else if (address >= 0x08000000 && address < 0x09FFFFFF) {
        result = rom[address - 0x08000000];
    }

    return result;
}

// -------------------------------------------------------------------
// Read a half‑word (16 bits) from the GBA address space
// -------------------------------------------------------------------
uint16_t Bus::read16(uint32_t address) {
    uint32_t addr = address & ~1;
    return (uint16_t)read8(addr) | ((uint16_t)read8(addr + 1) << 8);
}

// -------------------------------------------------------------------
// Read a word (32 bits) from the GBA address space
// -------------------------------------------------------------------
uint32_t Bus::read32(uint32_t address) {
    uint32_t alignedAddr = address & ~3;
    uint32_t val = (uint32_t)read8(alignedAddr)
        | ((uint32_t)read8(alignedAddr + 1) << 8)
        | ((uint32_t)read8(alignedAddr + 2) << 16)
        | ((uint32_t)read8(alignedAddr + 3) << 24);

    uint8_t shift = (address & 3) * 8;
    if (shift != 0) {
        val = (val >> shift) | (val << (32 - shift));
    }
    return val;
}

// -------------------------------------------------------------------
// Write a byte to the GBA address space
// -------------------------------------------------------------------
void Bus::write8(uint32_t address, uint8_t value) {
    // POSTFLG write – store separately
    if (address == 0x04000300) {
        postflg = value;
        return;
    }

    // VCOUNT is read‑only – ignore writes
    if (address == 0x04000006) return;

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
    // Writes to ROM or other read‑only areas are ignored
}

// -------------------------------------------------------------------
// Write a half‑word (16 bits) to the GBA address space
// -------------------------------------------------------------------
void Bus::write16(uint32_t address, uint16_t value) {
    write8(address, value & 0xFF);
    write8(address + 1, (value >> 8) & 0xFF);
}

// -------------------------------------------------------------------
// Write a word (32 bits) to the GBA address space
// -------------------------------------------------------------------
void Bus::write32(uint32_t address, uint32_t value) {
    write8(address, value & 0xFF);
    write8(address + 1, (value >> 8) & 0xFF);
    write8(address + 2, (value >> 16) & 0xFF);
    write8(address + 3, (value >> 24) & 0xFF);
}

// -------------------------------------------------------------------
// Advance the LCD scanline counter (call after every CPU step)
// -------------------------------------------------------------------
void Bus::tick() {
    step_counter++;
    if (step_counter >= 280) {
        step_counter = 0;
        vcount = (vcount + 1) % 228;
        io[6] = vcount;        // VCOUNT low byte at 0x04000006

        if (vcount >= 160)
            io[4] |= 1;        // DISPSTAT bit 0 = VBlank active
        else
            io[4] &= ~1;       // VBlank inactive during visible lines
    }
}

// -------------------------------------------------------------------
// Load the BIOS image into memory
// -------------------------------------------------------------------
void Bus::loadBIOS(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cout << "failed to load BIOS\n";
        return;
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size != 0x4000) {
        std::cout << "WARNING: BIOS size is " << size << " bytes, expected 16384 (0x4000)\n";
    }
    f.read((char*)bios.data(), std::min(size, (std::streamsize)0x4000));
    std::cout << "BIOS loaded, first 4 bytes: ";
    for (int i = 0; i < 4; i++) printf("%02X ", bios[i]);
    printf("\n");
}

// -------------------------------------------------------------------
// Load the ROM image into memory
// -------------------------------------------------------------------
void Bus::loadROM(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cout << "failed to load ROM — file not found: " << path << "\n";
        return;
    }
    // Read up to ROM size (32 MB)
    f.read((char*)rom.data(), 0x2000000);
    std::cout << "ROM loaded, first 4 bytes: ";
    for (int i = 0; i < 4; i++) {
        printf("%02X ", rom[i]);
    }
    printf("\n");
}