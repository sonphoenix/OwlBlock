#include "bus.h"
#include "cpu.h"
#include "PPU.h"
#include "apu.h"
#include "gba_registers.h"
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
   //dbg << "saaar VBLANNNNKK SAAAR \n";dbg.flush();
    for (int ch = 0; ch < 4; ch++) {
        if (!(dma[ch].cnt & (1u << 31))) continue;
        if (((dma[ch].cnt >> 28) & 0x3) != 1) continue;

        uint8_t dest_adj = (dma[ch].cnt >> 21) & 0x3;
        if (dest_adj == 3) dma[ch].idad = dma[ch].dad;
        executeDMA(ch);
    }
}

void Bus::onHBlank() {
    //dbg << "saaar HBLANNNNKK here \n";dbg.flush();

    for (int ch = 0; ch < 4; ch++) {
        if (!(dma[ch].cnt & (1u << 31))) continue;
        if (((dma[ch].cnt >> 28) & 0x3) != 2) continue;

        uint8_t dest_adj = (dma[ch].cnt >> 21) & 0x3;
        if (dest_adj == 3) dma[ch].idad = dma[ch].dad;
        executeDMA(ch);
    }
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
void Bus::detectEEPROMSize() {

    uint32_t romSize = 0;
    for (int i = (int)rom.size() - 1; i >= 0; i--) {
        if (rom[i] != 0) { romSize = (uint32_t)i + 1; break; }
    }
    eepromLargeAddress = (romSize >= 0x1000000);
    dbg << "[EEPROM_SIZE] romSize=0x" << std::hex << romSize
        << " eepromLargeAddress=" << eepromLargeAddress << "\n";
    dbg.flush();
}



void Bus::writeEEPROM(uint8_t bit) {
    switch (eepromState) {
    case EEPROM_IDLE:
        eepromBuffer = bit;
        eepromBitsLeft = 1;
        eepromState = EEPROM_REQUEST;
        eepromWriteDone = false;
        break;

    case EEPROM_REQUEST:
        eepromBuffer = (eepromBuffer << 1) | bit;
        eepromBitsLeft++;
        if (eepromBitsLeft == 2) {
            eepromRequestType = eepromBuffer & 0x3;
            eepromAddress = 0;
            eepromBitsLeft = eepromLargeAddress ? 14 : 6;
            eepromState = EEPROM_ADDRESS;
        }
        break;

    case EEPROM_ADDRESS:
        eepromAddress = (eepromAddress << 1) | bit;
        eepromBitsLeft--;
        if (eepromBitsLeft == 0) {
            if (eepromRequestType == 0x3) {
                eepromState = EEPROM_READ_DUMMY;
                eepromBitsLeft = 5;  // 4 dummy + 1 to absorb stop bit from write DMA
                uint32_t offset = eepromAddress * 8;
                for (int i = 0; i < 8; i++)
                    eepromReadBuffer[i] = (offset + i < (int)saveData.size())
                    ? saveData[offset + i] : 0xFF;
                eepromReadBit = 0;
                /*dbg << "[EEPROM_READ_SETUP] address=0x" << std::hex << eepromAddress
                    << " saveData[0]=0x" << (int)saveData[offset]
                    << " saveData[1]=0x" << (int)saveData[offset + 1]
                    << " saveData[2]=0x" << (int)saveData[offset + 2]
                    << " saveData[3]=0x" << (int)saveData[offset + 3] << "\n";
                dbg << "[EEPROM_TXN] READ requestType=0x" << std::hex << (int)eepromRequestType
                    << " address=0x" << eepromAddress
                    << " byteOffset=0x" << offset << "\n";
                dbg.flush();*/
            }
            else {
                eepromState = EEPROM_WRITE_DATA;
                eepromBitsLeft = 64;
                eepromBuffer = 0;
                /*dbg << "[EEPROM_TXN] WRITE_START requestType=0x" << std::hex << (int)eepromRequestType
                    << " address=0x" << eepromAddress << "\n";
                dbg.flush();*/
            }
        }
        break;

    case EEPROM_WRITE_DATA:
        eepromBuffer = (eepromBuffer << 1) | bit;
        eepromBitsLeft--;
        if (eepromBitsLeft == 0) {
            uint32_t offset = eepromAddress * 8;
            for (int i = 0; i < 8; i++) {
                uint8_t byte = (eepromBuffer >> (56 - i * 8)) & 0xFF;
                if (offset + i < saveData.size())
                    saveData[offset + i] = byte;
            }
           /* dbg << "[EEPROM_TXN] WRITE_COMPLETE address=0x" << std::hex << eepromAddress
                << " data=0x" << eepromBuffer << "\n";
            dbg << "[EEPROM_VERIFY] saveData[0]=0x" << (int)saveData[0]
                << " saveData[1]=0x" << (int)saveData[1]
                << " offset=" << std::dec << offset
                << " saveData.size()=" << saveData.size() << "\n";
            dbg.flush();*/
            eepromWriteDone = true;
            eepromState = EEPROM_WRITE_STOP;
            eepromBitsLeft = 1;
        }
        break;

    case EEPROM_WRITE_STOP:
        eepromState = EEPROM_IDLE;
        break;

    case EEPROM_READ_DUMMY:
        eepromBitsLeft--;
        if (eepromBitsLeft == 0) {
            eepromState = EEPROM_READ_DATA;
            eepromBitsLeft = 64;
        }
        break;

    case EEPROM_READ_DATA:
        eepromBitsLeft--;
        if (eepromBitsLeft == 0)
            eepromState = EEPROM_IDLE;
        break;

    default:
        break;
    }
}




uint8_t Bus::readEEPROM() {
    dbg << "[EEPROM_READ] state=" << (int)eepromState
        << " PC=0x" << std::hex << cpuptr->reg[15] << "\n";
    dbg.flush();

    switch (eepromState) {
    case EEPROM_READ_DUMMY:
        eepromBitsLeft--;
        if (eepromBitsLeft == 0) {
            eepromState = EEPROM_READ_DATA;
            eepromBitsLeft = 64;
        }
        return 0;

    case EEPROM_READ_DATA: {
        int bitIndex = 63 - (eepromBitsLeft - 1);
        int byteIndex = bitIndex / 8;
        int bitPos = 7 - (bitIndex % 8);
        uint8_t bit = (eepromReadBuffer[byteIndex] >> bitPos) & 1;
        eepromBitsLeft--;
        if (eepromBitsLeft == 0) {
            eepromState = EEPROM_IDLE;
            uint64_t dataOut = 0;
            for (int i = 0; i < 8; i++) dataOut = (dataOut << 8) | eepromReadBuffer[i];
            dbg << "[EEPROM_TXN] READ_COMPLETE address=0x" << std::hex << eepromAddress
                << " data=0x" << dataOut << "\n";
            dbg.flush();
        }
        return bit;
    }

    default:
        return 1;  // EEPROM ready / not in a read transaction
    }
}



uint8_t Bus::read8(uint32_t address) {
    if (address >= 0x0D000000 && address <= 0x0DFFFFFF) {
        dbg << "[EEPROM_ACCESS] read addr=0x" << std::hex << address
            << " state=" << (int)eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (saveType == SAVE_EEPROM)
            return readEEPROM();
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
        if (saveType == SAVE_FLASH64 || saveType == SAVE_FLASH128)
            return readFlash(address);
        if (saveType == SAVE_SRAM)
            return (address - 0x0E000000 < saveData.size())
            ? saveData[address - 0x0E000000] : 0xFF;
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
            << " state=" << (int)eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (saveType == SAVE_EEPROM)
            return readEEPROM();
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
        dbg << "[STACK_READ] addr=0x" << std::hex << address
            << " val=0x" << val
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
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
            << " state=" << (int)eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (saveType == SAVE_EEPROM)
            return readEEPROM();
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

void Bus::executeDMA(int channel) {
   /* dbg << "[DMA_EXEC] ch=" << channel
        << " src=0x" << std::hex << dma[channel].isad
        << " dst=0x" << dma[channel].idad
        << " cnt=0x" << dma[channel].cnt
        << " R0_before=0x" << cpuptr->reg[0] << "\n";
    dbg.flush();*/

    bool isFIFO = (((dma[channel].cnt >> 28) & 0x3) == 3) &&
        (dma[channel].dad == 0x040000A0 ||
            dma[channel].dad == 0x040000A4);
    uint32_t src = dma[channel].isad;
    uint32_t dest = dma[channel].idad;
    uint32_t count;
    if (isFIFO) {
        count = 4;
    }
    else {
        count = dma[channel].cnt & 0x0000FFFF;
        if (count == 0)
            count = (channel == 3) ? 0x10000 : 0x4000;
    }
    if (saveType == SAVE_EEPROM &&
        dma[channel].dad >= 0x0D000000 && dma[channel].dad <= 0x0DFFFFFF) {
        if (count == 73) eepromLargeAddress = false;
        else if (count == 81) eepromLargeAddress = true;
    }
    uint8_t dest_adj = isFIFO ? 2 : (dma[channel].cnt >> 21) & 0x3;
    uint8_t src_adj = (dma[channel].cnt >> 23) & 0x3;
    uint8_t repeat = (dma[channel].cnt >> 25) & 0x1;
    uint8_t word = (dma[channel].cnt >> 26) & 0x1;
    uint8_t irq = (dma[channel].cnt >> 30) & 0x1;
    uint8_t unit = word ? 4 : 2;
    for (uint32_t i = 0; i < count; i++) {
        if (word) write32(dest, read32(src));
        else      write16(dest, read16(src));
        switch (dest_adj) {
        case 0: dest += unit; break;
        case 1: dest -= unit; break;
        case 2: break;
        case 3: dest += unit; break;
        }
        switch (src_adj) {
        case 0: src += unit; break;
        case 1: src -= unit; break;
        case 2: break;
        }
    }
    dma[channel].isad = src;
    dma[channel].idad = (dest_adj == 3) ? dma[channel].dad : dest;
    if (saveType == SAVE_EEPROM &&
        dma[channel].dad >= 0x0D000000 && dma[channel].dad <= 0x0DFFFFFF) {
        if (eepromState == EEPROM_WRITE_DATA ||
            eepromState == EEPROM_WRITE_STOP ||
            eepromState == EEPROM_REQUEST ||
            eepromState == EEPROM_ADDRESS) {
            eepromState = EEPROM_IDLE;
        }
    }
    if (!repeat)
        dma[channel].cnt &= ~(1u << 31);
    if (irq) {
        uint16_t IF = io[0x202] | (io[0x203] << 8);
        IF |= (1 << (8 + channel));
        io[0x202] = IF & 0xFF;
        io[0x203] = (IF >> 8) & 0xFF;
    }

    dbg << "[DMA_EXEC_DONE] ch=" << channel
        << " R0_after=0x" << std::hex << cpuptr->reg[0] << "\n";
    dbg.flush();
}
void Bus::writeDMA(uint32_t address, uint8_t value) {
    int ch = (address - DMA0_BASE) / 12;
    int offset = (address - DMA0_BASE) % 12;

    /*dbg << "[DMA_WRITE] ch=" << ch << " offset=" << offset
        << " val=0x" << std::hex << (int)value
        << " PC=0x" << cpuptr->reg[15] << "\n";
    dbg.flush();*/

    if (offset < 4) { ((uint8_t*)&dma[ch].sad)[offset] = value; }
    else if (offset < 8) { ((uint8_t*)&dma[ch].dad)[offset - 4] = value; }
    else { ((uint8_t*)&dma[ch].cnt)[offset - 8] = value; }

    if (offset == 11) {
        if (dma[ch].cnt & (1u << 31)) {
            dma[ch].isad = dma[ch].sad;
            dma[ch].idad = dma[ch].dad;
            uint8_t tm = (dma[ch].cnt >> 28) & 0x3;
            if (tm == 0) executeDMA(ch);
        }
    }
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
            << " state=" << (int)eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (saveType == SAVE_EEPROM)
            writeEEPROM(value & 1);
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
        if (saveType == SAVE_FLASH64 || saveType == SAVE_FLASH128)
            writeFlash(address, value);
        else if (saveType == SAVE_SRAM) {
            uint32_t offset = address - 0x0E000000;
            if (offset < saveData.size())
                saveData[offset] = value;
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
            writeDMA(address, value);
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
            int timerIdx = (ioAddr - 0x100) / 4;
            int byteOff = (ioAddr - 0x100) % 4;

            if (byteOff == 0) {
                // Low byte of reload latch — don't touch running counter
                ((uint8_t*)&timer_reload[timerIdx])[0] = value;
            }
            else if (byteOff == 1) {
                // High byte of reload latch
                ((uint8_t*)&timer_reload[timerIdx])[1] = value;
            }
            else if (byteOff == 2) {
                // CNT_H low byte — bit 7 = start/stop
                bool was_on = (io[ioAddr] & 0x80) != 0;
                bool now_on = (value & 0x80) != 0;
                io[ioAddr] = value;
                if (!was_on && now_on) {
                    // Reload counter from latch on 0→1 start edge
                    io[0x100 + timerIdx * 4] = timer_reload[timerIdx] & 0xFF;
                    io[0x101 + timerIdx * 4] = (timer_reload[timerIdx] >> 8) & 0xFF;
                }
            }
            else {
                // byteOff == 3: CNT_H high byte (unused on GBA)
                io[ioAddr] = value;
            }
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
            dbg << "[IE_WRITE] addr=0x" << std::hex << ioAddr
                << " val=0x" << (int)value
                << " IE_after=0x" << (io[0x200] | (io[0x201] << 8))
                << " PC=0x" << cpuptr->reg[15] << "\n";
            dbg.flush();
            io[ioAddr] = value;
            break;
        case 0x208:
        case 0x209:
            dbg << "[IME_WRITE 208/209] addr=0x" << std::hex << ioAddr
                << " val=0x" << (int)value
                << " IME_after=0x" << (io[0x208] | (io[0x209] << 8))
                << " PC=0x" << cpuptr->reg[15] << "\n";
            dbg.flush();
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
        if (saveType == SAVE_EEPROM)
            writeEEPROM(value & 1);
        return;
    }
}

void Bus::write16(uint32_t address, uint16_t value) {
    mdr = (uint32_t)value * 0x00010001u;

    if (address >= 0x0D000000 && address <= 0x0DFFFFFF) {
        dbg << "[EEPROM_ACCESS] write16 addr=0x" << std::hex << address
            << " val=0x" << value
            << " state=" << (int)eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (saveType == SAVE_EEPROM)
            writeEEPROM(value & 1);
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
            << " state=" << (int)eepromState
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
        if (saveType == SAVE_EEPROM)
            writeEEPROM(value & 1);
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
    static uint32_t timer_ticks[4] = { 0, 0, 0, 0 };
    const uint16_t prescaler_shifts[4] = { 0, 6, 8, 10 };
    bool overflowed[4] = { false, false, false, false };

    for (int i = 0; i < 4; i++) {
        uint16_t cnt_h = io[0x102 + i * 4] | (io[0x103 + i * 4] << 8);
        if (!(cnt_h & (1 << 7))) continue;

        bool should_increment = false;
        bool cascade_mode = (i > 0) && (cnt_h & (1 << 2));

        if (cascade_mode) {
            should_increment = overflowed[i - 1];
        }
        else {
            uint8_t prescaler_sel = cnt_h & 0x3;
            timer_ticks[i]++;
            if (timer_ticks[i] >= (1u << prescaler_shifts[prescaler_sel])) {
                timer_ticks[i] = 0;
                should_increment = true;
            }
        }

        if (should_increment) {
            uint16_t current_val = io[0x100 + i * 4] | (io[0x101 + i * 4] << 8);
            current_val++;

            if (current_val == 0) {
                overflowed[i] = true;
                current_val = timer_reload[i];

                if (cnt_h & (1 << 6)) {
                    uint16_t IF = io[0x202] | (io[0x203] << 8);
                    IF |= (1 << (3 + i));
                    io[0x202] = IF & 0xFF;
                    io[0x203] = (IF >> 8) & 0xFF;
                }

                // ── only on actual overflow ──
                if (apuptr) apuptr->onTimerOverflow(i);
            }

            io[0x100 + i * 4] = current_val & 0xFF;
            io[0x101 + i * 4] = (current_val >> 8) & 0xFF;
        }
    }

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


//--------------------------------------------------------------------
// detect save type
//--------------------------------------------------------------------
void Bus::detectSaveType() {
    const char* markers[] = {
        "EEPROM_V",
        "SRAM_V",
        "FLASH_V",
        "FLASH512_V",
        "FLASH1M_V"
    };
    SaveType types[] = {
        SAVE_EEPROM,
        SAVE_SRAM,
        SAVE_FLASH64,
        SAVE_FLASH64,
        SAVE_FLASH128
    };

    for (int i = 0; i < 5; i++) {
        const char* marker = markers[i];
        int markerLen = (int)strlen(marker);
        for (int j = 0; j < (int)rom.size() - markerLen; j++) {
            if (memcmp(rom.data() + j, marker, markerLen) == 0) {
                saveType = types[i];
                dbg << "[SAVE_TYPE_DETECTED] marker=\"" << marker
                    << "\" at romOffset=0x" << std::hex << j
                    << " saveType=" << std::dec << (int)saveType << "\n";
                dbg.flush();
                goto done;
            }
        }
    }
    dbg << "[SAVE_TYPE_DETECTED] no marker found, saveType=NONE/default="
        << std::dec << (int)saveType << "\n";
    dbg.flush();

done:
    switch (saveType) {
    case SAVE_SRAM:     saveData.assign(0x8000, 0xFF); break;
    case SAVE_EEPROM:   saveData.assign(0x2000, 0xFF); detectEEPROMSize(); break;
    case SAVE_FLASH64:  saveData.assign(0x10000, 0xFF); break;
    case SAVE_FLASH128: saveData.assign(0x20000, 0xFF); break;
    default: break;
    }

    dbg << "[SAVE_TYPE_FINAL] saveType=" << std::dec << (int)saveType
        << " saveData.size()=0x" << std::hex << saveData.size() << "\n";
    dbg.flush();
}
void Bus::saveToDisk(const char* path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return;
    }
    f.write((char*)saveData.data(), saveData.size());
}

void Bus::loadFromDisk(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    f.read((char*)saveData.data(), saveData.size());
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
    detectSaveType();
    loadFromDisk("game.sav");
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

uint8_t Bus::readFlash(uint32_t address) {
    uint32_t offset = (address - 0x0E000000) + flashBank * 0x10000;
    dbg << "[FLASH_READ] this=0x" << std::hex << (uintptr_t)this
        << " idMode=" << flashIdMode
        << " addr=0x" << address
        << " offset=0x" << offset
        << " bank=" << (int)flashBank
        << " state=" << (int)flashState
        << " erasePending=" << flashErasePending
        << " val=0x" << (int)((offset < saveData.size()) ? saveData[offset] : 0xFF)
        << " PC=0x" << cpuptr->reg[15] << "\n";
    dbg.flush();

    if (flashIdMode) {
        if ((address & 0xFFFF) == 0) return 0xC2;
        if ((address & 0xFFFF) == 1) return 0x09;
        return 0xFF;
    }

    if (flashWritePending && offset == flashWriteAddr) {
        flashWritePending = false;
        return flashWriteVal ^ 0x80;
    }

    // Only poll on the erased sector address
    if (flashErasePending && (offset & ~0xFFF) == flashEraseSectorStart) {
        flashErasePollCount++;
        if (flashErasePollCount < 4) {
            // bit6 toggles, bit7=0 = busy
            return (flashErasePollCount & 1) ? 0x40 : 0x00;
        }
        else {
            flashErasePending = false;
            return 0xFF; // done, erased
        }
    }

    return (offset < saveData.size()) ? saveData[offset] : 0xFF;
}


void Bus::writeFlash(uint32_t address, uint8_t value) {
    uint32_t offset = (address - 0x0E000000) + flashBank * 0x10000;
    uint32_t shortAddr = address & 0xFFFF;

   /* dbg << "[FLASH_WRITE] addr=0x" << std::hex << address
        << " shortAddr=0x" << shortAddr
        << " val=0x" << (int)value
        << " state=" << (int)flashState
        << " bank=" << (int)flashBank
        << " PC=0x" << cpuptr->reg[15] << "\n";
    dbg.flush();*/

    switch (flashState) {
    case FLASH_READ:
        if (shortAddr == 0x5555 && value == 0xAA)
            flashState = FLASH_CMD1;
        break;

    case FLASH_ERASE_CMD:
        if (shortAddr == 0x5555 && value == 0xAA) {
            flashEraseArmed = true;
            flashState = FLASH_CMD1;
        }
        else if (shortAddr == 0x5555 && value == 0x10) {
            std::fill(saveData.begin(), saveData.end(), 0xFF);
            flashState = FLASH_READ;
        }
        break;

    case FLASH_CMD1:
        if (shortAddr == 0x2AAA && value == 0x55)
            flashState = FLASH_CMD2;
        else {
            flashEraseArmed = false;
            flashState = FLASH_READ;
        }
        break;

    case FLASH_CMD2:
        flashState = FLASH_READ;
        if (flashEraseArmed && value == 0x30) {
            flashEraseArmed = false;
            uint32_t sectorStart = offset & ~0xFFF;
            for (uint32_t i = sectorStart; i < sectorStart + 0x1000 && i < saveData.size(); i++)
                saveData[i] = 0xFF;
            dbg << "[FLASH_ERASE_SECTOR] sectorStart=0x" << std::hex << sectorStart
                << " bank=" << (int)flashBank << "\n";
            dbg.flush();
            flashErasePending = true;
            flashEraseSectorStart = sectorStart;
            flashErasePollCount = 0;
        }
        else if (shortAddr == 0x5555) {
            flashEraseArmed = false;
            switch (value) {
            case 0x90:
                flashIdMode = true;
                dbg << "[FLASH_ID_MODE_SET] this=0x" << std::hex << (uintptr_t)this << "\n";
                dbg.flush();
                break;
            case 0xF0: flashIdMode = false; break;
            case 0xA0: flashState = FLASH_WRITE; break;
            case 0x80: flashState = FLASH_ERASE_CMD; break;
            case 0xB0: flashState = FLASH_BANK; break;
            }
        }
        break;

    case FLASH_WRITE:
        if (offset < saveData.size())
            saveData[offset] = value;
        flashWritePending = true;
        flashWriteAddr = offset;
        flashWriteVal = value;
        flashState = FLASH_READ;
        break;


    case FLASH_BANK:
        flashBank = value & 1;
        flashState = FLASH_READ;
        break;

    default:
        flashState = FLASH_READ;
        break;
    }
}