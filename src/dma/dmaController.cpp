#include "dma/DMAController.h"
#include "core/bus.h"
#include "common/gba_registers.h"

bool DMAController::writeReg(uint32_t address, uint8_t value) {
    int ch = (address - DMA0_BASE) / 12;
    int offset = (address - DMA0_BASE) % 12;

    if (offset < 4) { ((uint8_t*)&channels[ch].sad)[offset] = value; }
    else if (offset < 8) { ((uint8_t*)&channels[ch].dad)[offset - 4] = value; }
    else { ((uint8_t*)&channels[ch].cnt)[offset - 8] = value; }

    if (offset == 11 && (channels[ch].cnt & (1u << 31))) {
        channels[ch].isad = channels[ch].sad;
        channels[ch].idad = channels[ch].dad;
        uint8_t tm = (channels[ch].cnt >> 28) & 0x3;
        return tm == 0;  // caller triggers execute() only if true
    }
    return false;
}

void DMAController::execute(int channel, Bus& bus) {
    auto& ch = channels[channel];

    bool isFIFO = (((ch.cnt >> 28) & 0x3) == 3) &&
        (ch.dad == 0x040000A0 || ch.dad == 0x040000A4);
    uint32_t src = ch.isad;
    uint32_t dest = ch.idad;
    uint32_t count;
    if (isFIFO) {
        count = 4;
    }
    else {
        count = ch.cnt & 0x0000FFFF;
        if (count == 0) count = (channel == 3) ? 0x10000 : 0x4000;
    }

    if (bus.save.saveType == SaveController::SAVE_EEPROM &&
        ch.dad >= 0x0D000000 && ch.dad <= 0x0DFFFFFF) {
        if (count == 73 || count == 9)  bus.save.eepromLargeAddress = false;
        else if (count == 81 || count == 17) bus.save.eepromLargeAddress = true;
    }

    uint8_t dest_adj = isFIFO ? 2 : (ch.cnt >> 21) & 0x3;
    uint8_t src_adj = (ch.cnt >> 23) & 0x3;
    uint8_t repeat = (ch.cnt >> 25) & 0x1;
    uint8_t word = (ch.cnt >> 26) & 0x1;
    uint8_t irq = (ch.cnt >> 30) & 0x1;
    uint8_t unit = word ? 4 : 2;

    for (uint32_t i = 0; i < count; i++) {
        if (word) bus.write32(dest, bus.read32(src));
        else      bus.write16(dest, bus.read16(src));

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

    ch.isad = src;
    ch.idad = (dest_adj == 3) ? ch.dad : dest;

    if (bus.save.saveType == SaveController::SAVE_EEPROM &&
        ch.dad >= 0x0D000000 && ch.dad <= 0x0DFFFFFF) {
        auto& es = bus.save.eepromState;
        if (es == SaveController::EEPROM_WRITE_DATA ||
            es == SaveController::EEPROM_WRITE_STOP ||
            es == SaveController::EEPROM_REQUEST ||
            es == SaveController::EEPROM_ADDRESS) {
            es = SaveController::EEPROM_IDLE;
        }
    }

    if (!repeat) ch.cnt &= ~(1u << 31);

    if (irq) {
        uint16_t IF = bus.io[0x202] | (bus.io[0x203] << 8);
        IF |= (1 << (8 + channel));
        bus.io[0x202] = IF & 0xFF;
        bus.io[0x203] = (IF >> 8) & 0xFF;
    }

    uint32_t cyclesPerUnit = word ? 4 : 2;
    pendingCycles += 2 + count * cyclesPerUnit;
}

void DMAController::onVBlank(Bus& bus) {
    for (int ch = 0; ch < 4; ch++) {
        if (!(channels[ch].cnt & (1u << 31))) continue;
        if (((channels[ch].cnt >> 28) & 0x3) != 1) continue;
        uint8_t dest_adj = (channels[ch].cnt >> 21) & 0x3;
        if (dest_adj == 3) channels[ch].idad = channels[ch].dad;
        execute(ch, bus);
    }
}

void DMAController::onHBlank(Bus& bus) {
    for (int ch = 0; ch < 4; ch++) {
        if (!(channels[ch].cnt & (1u << 31))) continue;
        if (((channels[ch].cnt >> 28) & 0x3) != 2) continue;
        uint8_t dest_adj = (channels[ch].cnt >> 21) & 0x3;
        if (dest_adj == 3) channels[ch].idad = channels[ch].dad;
        execute(ch, bus);
    }
}