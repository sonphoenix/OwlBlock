#include "core/bus.h"
#include "core/cpu.h"
#include "video/PPU.h"
#include "audio/apu.h"
#include "dma/DMAController.h"
#include "common/gba_registers.h"
#include <algorithm> 
#include <cstring>
extern std::ofstream dbg;
Bus::Bus() :
    cpuptr(nullptr),       // Start with no CPU linked
    ppuptr(nullptr),
    apuptr(nullptr),
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
    io[0x00] = 0x01;   // DISPCNT low: Mode 0, display on, forced blank off
    io[0x01] = 0x00;
    io[0x88] = 0x00;    // SOUNDBIAS low  } = 0x0200
    io[0x89] = 0x02;    // SOUNDBIAS high 
    io[0x130] = 0xFF;
    io[0x131] = 0x03;
};

void Bus::updateBiosLatch(uint32_t val) { bios_latch = val; }

inline uint32_t mirror(uint32_t addr, uint32_t base, uint32_t size) {
    return (addr - base) & (size - 1);
}

void Bus::setCPU(CPU* _cpu) { cpuptr = _cpu; }

void Bus::onVBlank() {
    dmaController.onVBlank(*this);
}

void Bus::onHBlank() {
    dmaController.onHBlank(*this);
}

static inline bool isOpenBusRegion(uint32_t address) {
    // Mirrors the same region checks used in Bus::read8's dispatch chain.
    if (address < 0x4000) return false;                                // BIOS
    if (address >= 0x02000000 && address < 0x03000000) return false;   // EWRAM
    if (address >= 0x03000000 && address < 0x04000000) return false;   // IWRAM
    if (address >= 0x04000000 && address < 0x04000400) return false;   // IO
    if (address >= 0x05000000 && address < 0x06000000) return false;   // PRAM
    if (address >= 0x06000000 && address < 0x07000000) return false;   // VRAM
    if (address >= 0x07000000 && address < 0x08000000) return false;   // OAM
    if (address >= 0x0E000000 && address <= 0x0FFFFFFF) return false;  // SRAM/Flash
    if (address >= 0x08000000 && address < 0x0E000000) return false;   // ROM
    return true; // everything else (including 0x10000000+) = truly unmapped -> open bus
}


uint8_t Bus::read8(uint32_t address) {
    if (address >= 0x0D000000 && address <= 0x0DFFFFFF) {
        dbg << "[EEPROM_ACCESS] read addr=0x" << std::hex << address
            << " state=" << (int)save.eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (save.saveType == save.SAVE_EEPROM)
            return save.readEEPROM();
        return 0xFF;
    }

    uint8_t result = 0;
    bool valid = true;

    if (address < 0x4000) {
        if (cpuptr->reg[15] >= 0x4000) {
            result = (bios_latch >> ((address & 3) * 8)) & 0xFF;
        }
        else
            result = bios[address];
    }
    else if (address >= 0x02000000 && address < 0x03000000) {
        result = ewram[mirror(address, 0x02000000, 0x40000)];
    }
    else if (address >= 0x03000000 && address < 0x04000000) {
        result = iwram[mirror(address, 0x03000000, 0x8000)];
    }
    else if (address >= 0x04000000 && address < 0x04000400) {
        uint32_t ioAddr = address - 0x04000000;
        if (address == 0x04000300) result = postflg;
        else if (address == 0x04000006) result = vcount;
        else result = io[ioAddr];
    }
    else if (address >= 0x05000000 && address < 0x06000000) {
        result = pram[mirror(address, 0x05000000, 0x400)];
    }
    else if (address >= 0x06000000 && address < 0x07000000) {
        uint32_t offset = (address - 0x06000000) % 0x20000;
        if (offset >= 0x18000) offset -= 0x8000;
        result = vram[offset];
    }
    else if (address >= 0x07000000 && address < 0x08000000) {
        result = oam[mirror(address, 0x07000000, 0x400)];
    }
    else if (address >= 0x0E000000 && address <= 0x0FFFFFFF) {
        if (address >= 0x0F000000) return (mdr >> ((address & 3) * 8)) & 0xFF;
        if (save.saveType == save.SAVE_FLASH64 || save.saveType == save.SAVE_FLASH128)
            return save.readFlash(address);
        if (save.saveType == save.SAVE_SRAM)
            return (address - 0x0E000000 < save.saveData.size())
            ? save.saveData[address - 0x0E000000] : 0xFF;
        return 0xFF;
    }
    else if (address >= 0x08000000 && address < 0x0E000000) {
        result = rom[address & 0x1FFFFFF];
    }
    else {
        // truly unmapped — return open bus
        valid = false;
        return (mdr >> ((address & 3) * 8)) & 0xFF;
    }

    if (valid) {
        // Byte access mirrors the value across all 4 lanes of the MDR
        mdr = (uint32_t)result * 0x01010101u;
    }

    return result;
}
uint16_t Bus::read16(uint32_t address) {
    if ((address & ~1) >= 0x0D000000 && (address & ~1) <= 0x0DFFFFFF) {
        dbg << "[EEPROM_ACCESS] read16 addr=0x" << std::hex << address
            << " state=" << (int)save.eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (save.saveType == save.SAVE_EEPROM)
            return save.readEEPROM();
        return 0xFF;
    }
    uint32_t addr = address & ~1;
    uint16_t result = (uint16_t)read8(addr) | ((uint16_t)read8(addr + 1) << 8);
    // Only a genuine memory-backed read re-latches the MDR.
    // Open-bus reads must leave mdr untouched — nothing actually drove the bus.
    if (!isOpenBusRegion(addr)) {
        mdr = (uint32_t)result * 0x00010001u;
    }
    return result;
}
uint32_t Bus::read32(uint32_t address) {
    if (address >= 0x3007f88 && address <= 0x3007f9c) {
        uint32_t alignedAddr = address & ~3;
        uint32_t val = (uint32_t)read8(alignedAddr)
            | ((uint32_t)read8(alignedAddr + 1) << 8)
            | ((uint32_t)read8(alignedAddr + 2) << 16)
            | ((uint32_t)read8(alignedAddr + 3) << 24);
    }
    if ((address & ~3) == 0x03FFFFFC) {
        uint32_t alignedAddr = address & ~3;
        uint32_t val = (uint32_t)read8(alignedAddr)
            | ((uint32_t)read8(alignedAddr + 1) << 8)
            | ((uint32_t)read8(alignedAddr + 2) << 16)
            | ((uint32_t)read8(alignedAddr + 3) << 24);
        dbg << "[READ_3FFFFFC] val=0x" << std::hex << val
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }

    if ((address & ~3) >= 0x0D000000 && (address & ~3) <= 0x0DFFFFFF) {
        dbg << "[EEPROM_ACCESS] read32 addr=0x" << std::hex << address
            << " state=" << (int)save.eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (save.saveType == save.SAVE_EEPROM)
            return save.readEEPROM();
        return 0xFF;
    }

    static uint32_t lastLoggedAddr = 0;
    if (cpuptr->reg[15] >= 0x390 && cpuptr->reg[15] <= 0x3A0) {
        if (address != lastLoggedAddr) {
            dbg << "[LOOP_READ] addr=0x" << std::hex << address
                << " val=0x" << read8(address)
                << " PC=0x" << cpuptr->reg[15] << "\n";
            dbg.flush();
            lastLoggedAddr = address;
        }
    }
    if (address < 0x4000 && cpuptr->reg[15] >= 0x4000) {
        return bios_latch;
    }

    uint32_t alignedAddr = address & ~3;
    uint32_t result = (uint32_t)read8(alignedAddr)
        | ((uint32_t)read8(alignedAddr + 1) << 8)
        | ((uint32_t)read8(alignedAddr + 2) << 16)
        | ((uint32_t)read8(alignedAddr + 3) << 24);

    // Only a genuine memory-backed read re-latches the MDR.
    if (!isOpenBusRegion(alignedAddr)) {
        mdr = result;
    }

    return result;
}

// -------------------------------------------------------------------
// Write a byte to the GBA address space
// -------------------------------------------------------------------
void Bus::write8(uint32_t address, uint8_t value) {
    mdr = (uint32_t)value * 0x01010101u;

    if (address >= 0x0600C000 && address <= 0x0600C010) {
        dbg << "[VRAM_CB3_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if ((address & ~1) == 0x0303010C || address == 0x0303010C || address == 0x0303010D) {
        dbg << "[WRITE_303010C] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if (address >= 0x02000000 && address <= 0x02000003 && value != 0) {
        dbg << "[EWRAM_NONZERO_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if (address >= 0x03007FF8 && address <= 0x03007FFF) {
        dbg << "[IWRAM_TAIL_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if (address >= 0x0D000000 && address <= 0x0DFFFFFF) {
        dbg << "[EEPROM_ACCESS] write8 addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " state=" << (int)save.eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (save.saveType == save.SAVE_EEPROM)
            save.writeEEPROM(value & 1);
        return;
    }
    if (address == 0x02000219 || address == 0x0200021A) {
        dbg << "[CORRUPT_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15]
            << " R1=0x" << cpuptr->reg[1]
            << " R2=0x" << cpuptr->reg[2] << "\n";
        dbg.flush();
    }
    if (address == 0x0400010E) {
        dbg << "[TM3CNT_H_WRITE] val=0x" << std::hex << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if (address >= 0x0E000000 && address <= 0x0EFFFFFF) {
        if (save.saveType == save.SAVE_FLASH64 || save.saveType == save.SAVE_FLASH128)
            save.writeFlash(address, value);
        else if (save.saveType == save.SAVE_SRAM) {
            uint32_t offset = address - 0x0E000000;
            if (offset < save.saveData.size())
                save.saveData[offset] = value;
        }
        return;
    }
    if (address >= 0x02000000 && address < 0x03000000) {
        ewram[mirror(address, 0x02000000, 0x40000)] = value;
        return;
    }
    else if (address >= 0x03000000 && address < 0x04000000) {
        uint32_t offset = mirror(address, 0x03000000, 0x8000);
        iwram[offset] = value;
        if (offset >= 0x7FF8 && offset <= 0x7FFF) {
            /* dbg << "[IWRAM_TAIL] offset=0x" << std::hex << offset
                 << " value=0x" << (int)value
                 << " PC=0x" << cpuptr->reg[15] << "\n";
             dbg.flush();*/
        }
        return;
    }

    if (address >= 0x04000000 && address < 0x04000400) {
        uint32_t ioAddr = address - 0x04000000;

        if (address >= DMA0_BASE && address <= 0x040000DF) {
            int ch = (address - DMA0_BASE) / 12;
            if (dmaController.writeReg(address, value)) {
                dmaController.execute(ch, *this);
            }
            return;
        }

        // ── APU registers 0x60–0xA8 ──────────────────────────────────
        if (ioAddr >= 0x60 && ioAddr <= 0xA8) {
            io[ioAddr] = value;           // keep io[] in sync for reads
            if (apuptr) apuptr->writeRegister(ioAddr, value);
            return;
        }
        // ─────────────────────────────────────────────────────────────

        // ── Timer registers 0x100-0x10F ──────────────────────────────
        if (ioAddr >= 0x100 && ioAddr <= 0x10F) {
            timers.handleWrite(io, ioAddr, value);
            return;
        }
        // ─────────────────────────────────────────────────────────────

        switch (ioAddr) {
        case 0x04:
            io[0x04] = (io[0x04] & 0x07) | (value & 0x38);
            dbg << "[DISPSTAT_WRITE] val=0x" << std::hex << (int)value
                << " result=0x" << (int)io[0x04]
                << " PC=0x" << cpuptr->reg[15] << "\n";
            dbg.flush();
            break;
        case 0x05:
            io[0x05] = value;
            break;
        case 0x202:
        case 0x203:
            io[ioAddr] &= ~value;
            break;
        case 0x301:
            if (!(value & 0x80))
                cpuptr->halted = true;
            break;
        case 0x300:
            postflg = value;
            break;
        case 0x06:
        case 0x07:
            break;
        case 0x200:
        case 0x201:

            io[ioAddr] = value;
            break;
        case 0x208:
        case 0x209:

            io[ioAddr] = value;
            break;
        case 0x28: case 0x29: case 0x2A: case 0x2B:
            io[ioAddr] = value;
            {
                int32_t refX = (int32_t)(io[0x28] | (io[0x29] << 8) | (io[0x2A] << 16) | (io[0x2B] << 24));
                if (refX & 0x08000000) refX |= 0xF0000000; else refX &= 0x0FFFFFFF;
                ppuptr->bg2RefX = refX;
            }
            break;

        case 0x2C: case 0x2D: case 0x2E: case 0x2F:
            io[ioAddr] = value;
            {
                int32_t refY = (int32_t)(io[0x2C] | (io[0x2D] << 8) | (io[0x2E] << 16) | (io[0x2F] << 24));
                if (refY & 0x08000000) refY |= 0xF0000000; else refY &= 0x0FFFFFFF;
                ppuptr->bg2RefY = refY;
            }
            break;

        case 0x38: case 0x39: case 0x3A: case 0x3B:
            io[ioAddr] = value;
            {
                int32_t refX = (int32_t)(io[0x38] | (io[0x39] << 8) | (io[0x3A] << 16) | (io[0x3B] << 24));
                if (refX & 0x08000000) refX |= 0xF0000000; else refX &= 0x0FFFFFFF;
                ppuptr->bg3RefX = refX;
            }
            break;

        case 0x3C: case 0x3D: case 0x3E: case 0x3F:
            io[ioAddr] = value;
            {
                int32_t refY = (int32_t)(io[0x3C] | (io[0x3D] << 8) | (io[0x3E] << 16) | (io[0x3F] << 24));
                if (refY & 0x08000000) refY |= 0xF0000000; else refY &= 0x0FFFFFFF;
                ppuptr->bg3RefY = refY;
            }
            break;
        default:
            io[ioAddr] = value;
            break;
        }
        return;
    }

    if (address >= 0x05000000 && address < 0x06000000) {
        uint32_t offset = mirror(address, 0x05000000, 0x400);
        pram[offset & ~1] = value;
        pram[(offset & ~1) + 1] = value;
        return;
    }

    if (address >= 0x06000000 && address < 0x07000000) {
        uint32_t offset = (address - 0x06000000) % 0x20000;
        if (offset >= 0x18000) offset -= 0x8000;
        vram[offset & ~1] = value;
        vram[(offset & ~1) + 1] = value;
        return;
    }

    if (address >= 0x07000000 && address < 0x08000000) {
        return;
    }

    if (address >= 0x0D000000 && address <= 0x0DFFFFFF) {
        if (save.saveType == save.SAVE_EEPROM)
            save.writeEEPROM(value & 1);
        return;
    }
}

void Bus::write16(uint32_t address, uint16_t value) {
    mdr = (uint32_t)value * 0x00010001u;

    if (address >= 0x0D000000 && address <= 0x0DFFFFFF) {
        dbg << "[EEPROM_ACCESS] write16 addr=0x" << std::hex << address
            << " val=0x" << value
            << " state=" << (int)save.eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (save.saveType == save.SAVE_EEPROM)
            save.writeEEPROM(value & 1);
        return;
    }
    uint32_t addr = address & ~1;

    if (addr >= 0x05000000 && addr < 0x06000000) {
        uint32_t offset = mirror(addr, 0x05000000, 0x400);
        pram[offset] = value & 0xFF;
        pram[offset + 1] = (value >> 8) & 0xFF;
        return;
    }
    // OAM
    if (addr >= 0x07000000 && addr < 0x08000000) {
        uint32_t oamAddr = mirror(addr, 0x07000000, 0x400);
        oam[oamAddr] = value & 0xFF;
        oam[oamAddr + 1] = (value >> 8) & 0xFF;
        return;
    }

    // VRAM: 16-bit writes always go through, bypass byte-write rules
    if (addr >= 0x06000000 && addr < 0x07000000) {
        uint32_t offset = (addr - 0x06000000) % 0x20000;
        if (offset >= 0x18000) offset -= 0x8000;
        vram[offset] = value & 0xFF;
        vram[offset + 1] = (value >> 8) & 0xFF;
        return;
    }

    // Default: split into two write8 calls
    write8(addr, value & 0xFF);
    write8(addr + 1, (value >> 8) & 0xFF);
    mdr = (uint32_t)value * 0x00010001u;   // restore half-mirror after byte sub-writes
}
void Bus::write32(uint32_t address, uint32_t value) {
    mdr = value;

    uint32_t addr = address & ~3;
    if ((address & ~3) <= 0x03004ca4 && (address & ~3) + 3 >= 0x03004ca4) {
        dbg << "[WRITE_3004CA4] write32 val=0x" << std::hex << value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if (addr >= 0x0D000000 && addr <= 0x0DFFFFFF) {
        dbg << "[EEPROM_ACCESS] write32 addr=0x" << std::hex << addr
            << " val=0x" << value
            << " state=" << (int)save.eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (save.saveType == save.SAVE_EEPROM)
            save.writeEEPROM(value & 1);
        return;
    }

    if (addr <= 0x0400010E && addr + 3 >= 0x0400010E) {
        dbg << "[TM3CNT_H_WRITE32] addr=0x" << std::hex << addr
            << " val=0x" << value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }

    // FIFO_A = 0x040000A0, FIFO_B = 0x040000A4
    if (addr == 0x040000A0 || addr == 0x040000A4) {
        io[addr - 0x04000000] = value & 0xFF;
        io[addr - 0x04000000 + 1] = (value >> 8) & 0xFF;
        io[addr - 0x04000000 + 2] = (value >> 16) & 0xFF;
        io[addr - 0x04000000 + 3] = (value >> 24) & 0xFF;
        if (apuptr) apuptr->writeRegister(addr - 0x04000000, value);
        return;
    }

    if (addr >= 0x05000000 && addr < 0x06000000) {
        uint32_t offset = mirror(addr, 0x05000000, 0x400);
        pram[offset] = value & 0xFF;
        pram[offset + 1] = (value >> 8) & 0xFF;
        pram[offset + 2] = (value >> 16) & 0xFF;
        pram[offset + 3] = (value >> 24) & 0xFF;
        return;
    }

    if (addr >= 0x06000000 && addr < 0x07000000) {
        uint32_t offset = (addr - 0x06000000) % 0x20000;
        if (offset >= 0x18000) offset -= 0x8000;
        vram[offset] = value & 0xFF;
        vram[offset + 1] = (value >> 8) & 0xFF;
        vram[offset + 2] = (value >> 16) & 0xFF;
        vram[offset + 3] = (value >> 24) & 0xFF;
        return;
    }

    // OAM
    if (addr >= 0x07000000 && addr < 0x08000000) {
        uint32_t oamAddr = mirror(addr, 0x07000000, 0x400);
        oam[oamAddr] = value & 0xFF;
        oam[oamAddr + 1] = (value >> 8) & 0xFF;
        oam[oamAddr + 2] = (value >> 16) & 0xFF;
        oam[oamAddr + 3] = (value >> 24) & 0xFF;
        return;
    }

    // Default: split into four write8 calls
    write8(addr, value & 0xFF);
    write8(addr + 1, (value >> 8) & 0xFF);
    write8(addr + 2, (value >> 16) & 0xFF);
    write8(addr + 3, (value >> 24) & 0xFF);
    mdr = value;   // restore word value after byte sub-writes
}
void Bus::checkIRQ() {
    uint16_t IME = io[0x208] | (io[0x209] << 8);
    uint16_t IE = io[0x200] | (io[0x201] << 8);
    uint16_t IF = io[0x202] | (io[0x203] << 8);
   /*if (cpuptr->halted) {                          // ADD THIS BLOCK
        dbg << "[HALTED_CHECK] IE=0x" << std::hex << IE
            << " IF=0x" << IF << "\n";
        dbg.flush();
    }*/
    if (IE & IF) {
        if (cpuptr->halted) {
            dbg << "[UNHALT] IE=0x" << std::hex << IE << " IF=0x" << IF << "\n";
            dbg.flush();
        }
        cpuptr->halted = false;
    }
    if ((cpuptr->cpsr & 0x1F) == 0x12) return;
    if ((IME & 1) && (IE & IF) && !(cpuptr->cpsr & (1 << 7))) {
        cpuptr->triggerIRQ();
    }
}
// -------------------------------------------------------------------
// Advance the LCD scanline counter (call after every CPU step)
// -------------------------------------------------------------------
void Bus::tick() {
    step_counter++;

    // ── Timers ────────────────────────────────────────────────────────
    timers.tick(io, apuptr);

    // ── APU ──────────────────────────────────────────────────────────
    if (apuptr) apuptr->tick(1);


    // ── LCD timing ───────────────────────────────────────────────────
    if (step_counter == 960) {
        if (vcount < 160)
            ppuptr->renderScanLine(vcount);

        io[0x04] |= (1 << 1);  // HBlank flag

        if (io[0x04] & (1 << 4)) {
            uint16_t IF = io[0x202] | (io[0x203] << 8);
            IF |= (1 << 1);
            io[0x202] = IF & 0xFF;
            io[0x203] = (IF >> 8) & 0xFF;
        }

        if (vcount < 160) {
            onHBlank();
            win0h_scanline[vcount] = io[0x40] | (io[0x41] << 8);
        }
    }

    if (step_counter >= 1232) {
        step_counter = 0;

        io[0x04] &= ~(1 << 1);  // Clear HBlank flag

        vcount = (vcount + 1) % 228;
        io[0x06] = vcount & 0xFF;
        io[0x07] = 0;

        uint8_t vcount_trigger = io[0x05];
        if (vcount == vcount_trigger) {
            io[0x04] |= (1 << 2);
            if (io[0x04] & (1 << 5)) {
                uint16_t IF = io[0x202] | (io[0x203] << 8);
                IF |= (1 << 2);
                io[0x202] = IF & 0xFF;
                io[0x203] = (IF >> 8) & 0xFF;
            }
        }
        else {
            io[0x04] &= ~(1 << 2);
        }

        if (vcount == 160) {
            dbg << "[VBLANK_IF_SET] frame_cycle_total=" << step_counter
                << " vcount=" << (int)vcount << "\n";
            dbg.flush();
            io[0x04] |= 1;

            if (io[0x04] & (1 << 3)) {
                uint16_t IF = io[0x202] | (io[0x203] << 8);
                IF |= 1;
                io[0x202] = IF & 0xFF;
                io[0x203] = (IF >> 8) & 0xFF;
            }

            onVBlank();

            if (ppuptr) {
                auto se = [](int32_t v) -> int32_t {
                    return (v & 0x08000000) ? (v | 0xF0000000) : (v & 0x0FFFFFFF);
                    };
                ppuptr->bg2RefX = se((int32_t)(io[0x28] | (io[0x29] << 8) | (io[0x2A] << 16) | (io[0x2B] << 24)));
                ppuptr->bg2RefY = se((int32_t)(io[0x2C] | (io[0x2D] << 8) | (io[0x2E] << 16) | (io[0x2F] << 24)));
                ppuptr->bg3RefX = se((int32_t)(io[0x38] | (io[0x39] << 8) | (io[0x3A] << 16) | (io[0x3B] << 24)));
                ppuptr->bg3RefY = se((int32_t)(io[0x3C] | (io[0x3D] << 8) | (io[0x3E] << 16) | (io[0x3F] << 24)));
            }
        }
        else if (vcount == 0) {
            io[0x04] &= ~1;
        }
    }
    // IRQ dispatch removed — call checkIRQ() from main loop instead
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

    // NEW: initialise BIOS latch with first word of BIOS
    bios_latch = bios[0] | (bios[1] << 8) | (bios[2] << 16) | (bios[3] << 24);
    dbg << "[BIOS_08] " << std::hex
        << (int)bios[0x08] << " "
        << (int)bios[0x09] << " "
        << (int)bios[0x0a] << " "
        << (int)bios[0x0b] << "\n";

    dbg << "[BIOS_VERIFY] bytes at 0x390: ";
    for (int i = 0x390; i < 0x3A0; i++) {
        dbg << std::hex << (int)bios[i] << " ";
    }
    dbg << "\n";
    dbg.flush();
}


// -------------------------------------------------------------------
// Load the ROM image into memory
// -------------------------------------------------------------------
void Bus::loadROM(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cout << "failed to load ROM\n";
        return;
    }
    f.read((char*)rom.data(), 0x2000000);
    save.detectSaveType(rom);
    save.loadFromDisk("game.sav");
}

void Bus::setKeyState(int bit, bool pressed) {

    uint16_t keys = io[0x130] | (io[0x131] << 8);

    if (pressed)
        keys &= ~(1 << bit); // active low
    else
        keys |= (1 << bit);

    io[0x130] = keys & 0xFF;
    io[0x131] = (keys >> 8) & 0xFF;
}
