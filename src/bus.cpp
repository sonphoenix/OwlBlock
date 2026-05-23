#include "bus.h"
#include "cpu.h"
#include "PPU.h"
#include "gba_registers.h"
#include <algorithm> 
#include <cstring>
extern std::ofstream dbg;
static bool displayModeSet = false;
static int  c0cClearCount = 0;   // count calls from 0xC0C
Bus::Bus() :
    cpuptr(nullptr),       // Start with no CPU linked
    ppuptr(nullptr),
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

// -------------------------------------------------------------------
// Read a byte from the GBA address space
// -------------------------------------------------------------------
uint8_t Bus::read8(uint32_t address) {
    uint8_t result = 0;
    if (cpuptr->reg[15] >= 0x3003580 && cpuptr->reg[15] <= 0x3003700) {
        dbg << "[HANDLER_EXEC] PC=0x" << std::hex << cpuptr->reg[15] << "\n";
    }
    if (address < 0x4000) {
        if (cpuptr->reg[15] >= 0x4000)
            result = (bios_latch >> ((address & 3) * 8)) & 0xFF;
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
        result = vram[(address - 0x06000000) % 0x18000];
    }
    else if (address >= 0x07000000 && address < 0x08000000) {
        result = oam[mirror(address, 0x07000000, 0x400)];
    }
    else if (address >= 0x08000000) {
        result = rom[address & 0x1FFFFFF];
    }

    if (cpuptr->reg[15] >= 0x80008BE && cpuptr->reg[15] <= 0x80008D0) {
        dbg << "[MAINLOOP_READ] addr=0x" << std::hex << address
            << " val=0x" << (int)result << "\n";
    }

    return result;
}// -------------------------------------------------------------------
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
    if (address >= 0x10000000 && address <= 0x1FFFFFFF) {
        dbg << "BAD READ32 at addr=0x" << std::hex << address << "\n";
    }
    if (address < 0x4000 && cpuptr->reg[15] >= 0x4000) {
        return bios_latch;
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
    uint32_t src = dma[channel].isad;
    uint32_t dest = dma[channel].idad;
    uint32_t count = dma[channel].cnt & 0x0000FFFF;

    dbg << "[DMA] ch=" << channel
        << " src=0x" << std::hex << src
        << " dst=0x" << dest
        << " count=" << std::dec << count << "\n";
    dbg.flush();

    if (count == 0)
        count = (channel == 3) ? 0x10000 : 0x4000;

    uint8_t dest_adj = (dma[channel].cnt >> 21) & 0x3;
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

    if (!repeat)
        dma[channel].cnt &= ~(1u << 31);

    // FIXED: Properly write to the 16-bit IF register across two 8-bit bytes
    if (irq) {
        uint16_t IF = io[0x202] | (io[0x203] << 8);
        IF |= (1 << (8 + channel));
        io[0x202] = IF & 0xFF;
        io[0x203] = (IF >> 8) & 0xFF;
    }
}


void Bus::writeDMA(uint32_t address, uint8_t value) {
    if (address >= DMA0_BASE && address <= DMA0_BASE + 11) {
        dbg << "[DMA0_REG] offset=" << std::dec << (address - DMA0_BASE)
            << " val=0x" << std::hex << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }

    int ch = (address - DMA0_BASE) / 12;
    int offset = (address - DMA0_BASE) % 12;
    if (offset == 7) {  // last byte of DAD
        dbg << "[DMA_DAD] ch=" << ch
            << " dad=0x" << std::hex << dma[ch].dad << "\n";
        dbg.flush();
    }
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
    if (address == 0x3003f3c && value == 0 && cpuptr->reg[15] < 0x4000) {
        iwram[mirror(0x3003f3c, 0x03000000, 0x8000)] = 0x40;
        return;
    }
    if (address == 0x05000000 || address == 0x05000001) {
        dbg << "[PRAM0_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if (address >= 0x05000200 && address <= 0x05000203) {
        dbg << "[OBJ_PAL_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if (address >= 0x3003f3c && address <= 0x3003f3f) {
        dbg << "[FRAME_PTR_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    if (address == 0x3003f3c) {
        dbg << "[F3C_WRITE] val=0x" << std::hex << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }
    // ==== IRQ handler copy logging ====
    if (address >= 0x3003580 && address <= 0x30035FF) {
        dbg << "[IWRAM_IRQ_COPY] addr=0x" << std::hex << address
            << " value=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }

    // ==== First 4 bytes of IWRAM (optional) ====
    if (address >= 0x3000000 && address <= 0x3000003) {
        dbg << "[DISP_VAR_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }

    // ==== Flag monitoring (0x300310C-0x300310D) ====
    if (address >= 0x0300310c && address <= 0x0300310d) {
        dbg << "[FLAG_WRITE] addr=0x" << std::hex << address
            << " val=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }

    // ==== IRQ vector writes (0x3007FF8-0x3007FFF) ====
    if (address >= 0x3007FF8 && address <= 0x3007FFF) {
        dbg << "[IRQ_VECTOR_WRITE] addr=0x" << std::hex << address
            << " value=0x" << (int)value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }

    // ==== Actual memory writes ====
    // EWRAM (256KB mirrored)
    if (address >= 0x02000000 && address < 0x03000000) {
        ewram[mirror(address, 0x02000000, 0x40000)] = value;
        return;
    }
    // IWRAM (32KB mirrored)
    else if (address >= 0x03000000 && address < 0x04000000) {
        iwram[mirror(address, 0x03000000, 0x8000)] = value;
        return;
    }

    // I/O registers
    if (address >= 0x04000000 && address < 0x04000400) {
        uint32_t ioAddr = address - 0x04000000;

        // DMA registers
        if (address >= DMA0_BASE && address <= 0x040000DF) {
            writeDMA(address, value);
            return;
        }

        switch (ioAddr) {
        case 0x04:
            io[0x04] = (io[0x04] & 0x07) | (value & 0x38);
            dbg << "[DISPSTAT WRITE] value=0x" << std::hex << (int)value
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
        default:
            io[ioAddr] = value;
            if (ioAddr == 0x00 || ioAddr == 0x01) {
                uint16_t dispcnt = io[0x00] | (io[0x01] << 8);
                // Read what's actually stored in the display-mode variable right now
                uint32_t c0Addr = mirror(0x030000C0, 0x03000000, 0x8000);
                uint16_t c0val = iwram[c0Addr] | (iwram[c0Addr + 1] << 8);
                dbg << "[DISPCNT WRITE] addr=0x" << std::hex << address
                    << " value=0x" << (int)value
                    << " full=0x" << dispcnt
                    << " C0_VAL=0x" << c0val
                    << " PC=0x" << cpuptr->reg[15] << "\n";
                dbg.flush();
            }
            break;
        }
        return;
    }

    // Palette RAM (1KB mirror)
    if (address >= 0x05000000 && address < 0x06000000) {
        pram[mirror(address, 0x05000000, 0x400)] = value;
        return;
    }

    // VRAM (96KB mirror)
    if (address >= 0x06000000 && address < 0x07000000) {
        uint32_t vramAddr = (address - 0x06000000) % 0x18000;
        vram[vramAddr] = value;
        return;
    }

    // OAM – byte writes ignored on hardware
    if (address >= 0x07000000 && address < 0x08000000) {
        return;
    }
}

void Bus::write16(uint32_t address, uint16_t value) {
    uint32_t addr = address & ~1;

    // IWRAM IRQ copy logging (if you still want it)
    if (address >= 0x3003580 && address <= 0x30035FF) {
        dbg << "[IWRAM_IRQ_COPY16] addr=0x" << std::hex << address
            << " value=0x" << value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
    }

    // OAM
    if (addr >= 0x07000000 && addr < 0x08000000) {
        uint32_t oamAddr = mirror(addr, 0x07000000, 0x400);
        oam[oamAddr] = value & 0xFF;
        oam[oamAddr + 1] = (value >> 8) & 0xFF;
        return;
    }

    // Default: split into two write8 calls
    write8(addr, value & 0xFF);
    write8(addr + 1, (value >> 8) & 0xFF);
}


void Bus::write32(uint32_t address, uint32_t value) {
    uint32_t addr = address & ~3;


    // IWRAM IRQ copy logging (if you still want it)
    if (address >= 0x3003580 && address <= 0x30035FF) {
        dbg << "[IWRAM_IRQ_COPY32] addr=0x" << std::hex << address
            << " value=0x" << value
            << " PC=0x" << cpuptr->reg[15] << "\n";
        dbg.flush();
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
}
// -------------------------------------------------------------------
// Advance the LCD scanline counter (call after every CPU step)
// -------------------------------------------------------------------
void Bus::tick() {
    step_counter++;

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
                uint16_t reload_val = io[0x100 + i * 4] | (io[0x101 + i * 4] << 8);
                current_val = reload_val;

                if (cnt_h & (1 << 6)) {
                    uint16_t IF = io[0x202] | (io[0x203] << 8);
                    IF |= (1 << (3 + i));
                    io[0x202] = IF & 0xFF;
                    io[0x203] = (IF >> 8) & 0xFF;
                }
            }

            io[0x100 + i * 4] = current_val & 0xFF;
            io[0x101 + i * 4] = (current_val >> 8) & 0xFF;
        }
    }

    if (step_counter == 960) {
        if (vcount < 160)
            ppuptr->renderScanLine(vcount);

        io[0x04] |= (1 << 1);

        if (io[0x04] & (1 << 4)) {
            uint16_t IF = io[0x202] | (io[0x203] << 8);
            IF |= (1 << 1);
            io[0x202] = IF & 0xFF;
            io[0x203] = (IF >> 8) & 0xFF;
        }

        if (vcount < 160) {
            for (int i = 0; i < 4; i++) {
                if ((dma[i].cnt & (1u << 31)) &&
                    (((dma[i].cnt >> 28) & 0x3) == 2))
                    executeDMA(i);
            }
            win0h_scanline[vcount] = io[0x40] | (io[0x41] << 8);
        }
    }

    if (step_counter >= 1232) {
        step_counter = 0;

        io[0x04] &= ~(1 << 1);

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
            io[0x04] |= 1;

            if (io[0x04] & (1 << 3)) {
                uint16_t IF = io[0x202] | (io[0x203] << 8);
                IF |= 1;
                io[0x202] = IF & 0xFF;
                io[0x203] = (IF >> 8) & 0xFF;
            }

            for (int i = 0; i < 4; i++) {
                if ((dma[i].cnt & (1u << 31)) &&
                    (((dma[i].cnt >> 28) & 0x3) == 1))
                    executeDMA(i);
            }

           /* uint16_t biosFlags = iwram[0x7FF8] | (iwram[0x7FF9] << 8);
            biosFlags |= 1;
            iwram[0x7FF8] = biosFlags & 0xFF;
            iwram[0x7FF9] = (biosFlags >> 8) & 0xFF;*/
        }
        else if (vcount == 0) {
            io[0x04] &= ~1;
        }
    }

    static uint16_t prev_pending = 0;

    uint16_t IME = io[0x208] | (io[0x209] << 8);
    uint16_t IE = io[0x200] | (io[0x201] << 8);
    uint16_t IF = io[0x202] | (io[0x203] << 8);

    bool hardware_wakeup = (IE & IF) != 0;
    if (hardware_wakeup)
        cpuptr->halted = false;

    bool irq_pending = (IME & 1) && (IE & prev_pending) != 0;
    if (irq_pending) {
        if (!(cpuptr->cpsr & (1 << 7)))
            cpuptr->triggerIRQ();
    }

    prev_pending = IF;
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


void Bus::setKeyState(int bit, bool pressed) {

    uint16_t keys = io[0x130] | (io[0x131] << 8);

    if (pressed)
        keys &= ~(1 << bit); // active low
    else
        keys |= (1 << bit);

    io[0x130] = keys & 0xFF;
    io[0x131] = (keys >> 8) & 0xFF;
}