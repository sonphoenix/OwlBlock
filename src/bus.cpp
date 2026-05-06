#include "bus.h"
#include "cpu.h"
#include "gba_registers.h"
#include <algorithm> 
#include <cstring>
extern std::ofstream dbg;
Bus::Bus() :
    cpuptr(nullptr),       // Start with no CPU linked
    bios_latch(0),      // Initial BIOS latch is 0
    postflg(0),
    vcount(0),
    step_counter(0) {
    bios.assign(0x4000, 0);      // 16 KB
    ewram.assign(0x40000, 0);    // 256 KB
    iwram.assign(0x8000, 0);     // 32 KB
    io.assign(0x400, 0);         // 1 KB
    pram.assign(0x400, 0);       // 1 KB
    vram.assign(0x18000, 0);     // 96 KB
    oam.assign(0x400, 0);        // 1 KB
    rom.assign(0x2000000, 0);    // 32 MB (Max size)
    io[0x130] = 0xFF;
    io[0x131] = 0x03;
};

void Bus::updateBiosLatch(uint32_t val) { bios_latch = val; }


void Bus::setCPU(CPU* _cpu) { cpuptr = _cpu; }

// -------------------------------------------------------------------
// Read a byte from the GBA address space
// -------------------------------------------------------------------
uint8_t Bus::read8(uint32_t address) {
    uint8_t result = 0;

    if (address < 0x4000) {
        if (cpuptr->reg[15] >= 0x4000) {
           // std::cout << "rom dumping detected saaar\n";
;            return (bios_latch >> ((address & 3) * 8)) & 0xFF;
        }
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
    if (address < 0x4000 && cpuptr->reg[15] >= 0x4000) {
        return bios_latch;
    }
    if (address == 0x03007FFC) {
        dbg << "BIOS reading ISR ptr = 0x" << std::hex
            << *(uint32_t*)&iwram[0x7FFC] << "\n";
        dbg.flush();
    }
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


void Bus::executeDMA(int channel) {
    uint32_t src = dma[channel].sad;
    uint32_t dest = dma[channel].dad;
    uint16_t count = dma[channel].cnt & 0x0000FFFF;
    uint8_t dest_adj = (dma[channel].cnt >> 21) & 0x3;
    uint8_t src_adj= (dma[channel].cnt >> 23) & 0x3;
    uint8_t repeat = (dma[channel].cnt >> 25) & 1;
    uint8_t word = (dma[channel].cnt >> 26) & 1;
    uint8_t tm = (dma[channel].cnt >> 28) & 0x3;
    uint8_t irq= (dma[channel].cnt >> 30) & 1;
    uint8_t en = (dma[channel].cnt >> 31) & 1;

    uint8_t unit = (word) ? 4 : 2;
    for (int i = 0;i < count;i++) {
        if (word) {
            write32(dest, read32(src));

        }
        else
        {
            write16(dest, read16(src));
        }
        switch (dest_adj) {
        case 0: dest += unit; break;
        case 1: dest -= unit; break;
        case 2: break;
        }
        switch (src_adj) {
        case 0: src += unit; break;
        case 1: src -= unit; break;
        case 2: break;
        }
    }
    if (!repeat)
        dma[channel].cnt &= ~(1 << 31);
    if (irq)
        io[REG_IF] |= (1 << (8 + channel));
    
}

void Bus::writeDMA(uint32_t address, uint8_t value) {
    int ch = (address - DMA0_BASE) / 12;
    int offset = (address - DMA0_BASE) % 12;

    uint8_t* base = (uint8_t*) &dma[ch];
    base[offset] = value;
    if (offset == 11 && value&0x80) {
        uint8_t tm = (dma[ch].cnt >> 28) & 0x3;
        if (tm == 0) {// immediate
            executeDMA(ch);
        }

    }

}



// -------------------------------------------------------------------
// Write a byte to the GBA address space
// -------------------------------------------------------------------
void Bus::write8(uint32_t address, uint8_t value) {
    // 1. Handle Work RAM (EWRAM / IWRAM)
    if (address >= 0x02000000 && address < 0x02040000) {
        ewram[address - 0x02000000] = value;
        return;
    }
    if (address >= 0x03000000 && address < 0x03008000) {
        iwram[address - 0x03000000] = value;
        return;
    }

    // 2. Handle I/O Registers
    if (address >= 0x04000000 && address < 0x04000400) {
        if (address >= DMA0_BASE && address <= 0x040000DF) {
            Bus::writeDMA(address, value);
            return;
        }
        uint32_t ioAddr = address - 0x04000000;

        switch (ioAddr) {
        case 0x202:
        case 0x203:
            io[ioAddr] &= ~value;
            break;

            // VCOUNT (0x04000006)
            // This is Read-Only. Ignore any writes.
        case 0x06:
        case 0x07:
            break;

            // POSTFLG (0x04000300)
        case 0x300:
            postflg = value;
            break;

            // HALTCNT (0x04000301)
        case 0x301:
            if (value & 0x80) {
                // Logic for Stop/Halt would go here
            }
            else {
                dbg << " cpu halted shhhheeeesh \n";
                dbg.flush();
                cpuptr->halted = true;
            }
            break;
        case 0xD8:

            // Default behavior for other I/O
        default:
            io[ioAddr] = value;
            break;
        }
        return;
    }

    // 3. Handle Palette, VRAM, and OAM
    if (address >= 0x05000000 && address < 0x05000400) {
        pram[address - 0x05000000] = value;
    }
    else if (address >= 0x06000000 && address < 0x06018000) {
        vram[address - 0x06000000] = value;
    }
    else if (address >= 0x07000000 && address < 0x07000400) {
        oam[address - 0x07000000] = value;
    }

    // 4. ROM and BIOS are Read-Only (Ignore writes)
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
    if (address == 0x03007FFC) {
        dbg << "STORE to [03007FFC] val=0x" << std::hex << value
            << " PC=0x" << cpuptr->reg[15] - 4  // -4 because Step already advanced
            << " R0=0x" << cpuptr->reg[0]
            << " R1=0x" << cpuptr->reg[1] << "\n";
        dbg.flush();
    }
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
    if (step_counter >= 280) { // Standard GBA scanline is ~1232 cycles, but 280 is fine for basic timing
        step_counter = 0;
        vcount = (vcount + 1) % 228;
        io[6] = vcount;

        if (vcount == 160) {
            for (int i = 0;i < 4;i++) {
                int8_t tm = (dma[i].cnt >> 28) & 0x3;
                if (((dma[i].cnt >> 31) & 1) && tm == 1) {
                    executeDMA(i); //vblank DMA
                }
            }
        
            io[4] |= 1; 
            uint16_t currentIF = io[0x202] | (io[0x203] << 8);
            currentIF |= 1;
            io[0x202] = currentIF & 0xFF;
            io[0x203] = (currentIF >> 8) & 0xFF;

            dbg << "VBLANK! IME=" << (int)read16(0x4000208) << " IF=" << currentIF << "\n";
        }
        else if (vcount == 0) {
            io[4] &= ~1;
        }

        for (int i = 0;i < 4;i++) {
            int8_t tm = (dma[i].cnt >> 28) & 0x3;
            if (((dma[i].cnt >> 31) & 1) && tm == 2) {
                executeDMA(i); //hblank DMA
            }
        }
    }

    uint16_t IME = read16(0x04000208);
    uint16_t IE = read16(0x04000200);
    uint16_t IF = read16(0x04000202);

    if ((IME & 1) && (IE & IF) && !(cpuptr->cpsr & (1 << 7))) {
        dbg << "irq triggered saaaaaar \n";
        dbg.flush();
        cpuptr->triggerIRQ();
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
    f.read((char*)rom.data(), 0x2000000);

}