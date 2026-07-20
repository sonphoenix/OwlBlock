#include "save/SaveController.h"
#include <fstream>
#include <cstring>
#include <algorithm>
extern std::ofstream dbg;

void SaveController::detectEEPROMSize(const std::vector<uint8_t>& rom) {
    uint32_t romSize = 0;
    for (int i = (int)rom.size() - 1; i >= 0; i--) {
        if (rom[i] != 0) { romSize = (uint32_t)i + 1; break; }
    }
    eepromLargeAddress = (romSize >= 0x1000000);
    dbg << "[EEPROM_SIZE] romSize=0x" << std::hex << romSize
        << " eepromLargeAddress=" << eepromLargeAddress << "\n";
    dbg.flush();
}

void SaveController::writeEEPROM(uint8_t bit) {
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
                eepromBitsLeft = 5;
                uint32_t offset = eepromAddress * 8;
                for (int i = 0; i < 8; i++)
                    eepromReadBuffer[i] = (offset + i < (int)saveData.size())
                    ? saveData[offset + i] : 0xFF;
                eepromReadBit = 0;
            }
            else {
                eepromState = EEPROM_WRITE_DATA;
                eepromBitsLeft = 64;
                eepromBuffer = 0;
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

uint8_t SaveController::readEEPROM() {
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
        }
        return bit;
    }

    default:
        return 1;
    }
}

void SaveController::detectSaveType(const std::vector<uint8_t>& rom) {
    const char* markers[] = {
        "EEPROM_V", "SRAM_V", "FLASH_V", "FLASH512_V", "FLASH1M_V"
    };
    SaveType types[] = {
        SAVE_EEPROM, SAVE_SRAM, SAVE_FLASH64, SAVE_FLASH64, SAVE_FLASH128
    };

    for (int i = 0; i < 5; i++) {
        const char* marker = markers[i];
        int markerLen = (int)strlen(marker);
        for (int j = 0; j < (int)rom.size() - markerLen; j++) {
            if (memcmp(rom.data() + j, marker, markerLen) == 0) {
                saveType = types[i];
                goto done;
            }
        }
    }

done:
    switch (saveType) {
    case SAVE_SRAM:     saveData.assign(0x8000, 0xFF); break;
    case SAVE_EEPROM:   saveData.assign(0x2000, 0xFF); detectEEPROMSize(rom); break;
    case SAVE_FLASH64:  saveData.assign(0x10000, 0xFF); break;
    case SAVE_FLASH128: saveData.assign(0x20000, 0xFF); break;
    default: break;
    }
}

void SaveController::saveToDisk(const char* path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    f.write((char*)saveData.data(), saveData.size());
}

void SaveController::loadFromDisk(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return;
    f.read((char*)saveData.data(), saveData.size());
}

uint8_t SaveController::readFlash(uint32_t address) {
    uint32_t offset = (address - 0x0E000000) + flashBank * 0x10000;

    if (flashIdMode) {
        if ((address & 0xFFFF) == 0) return 0xC2;
        if ((address & 0xFFFF) == 1) return 0x09;
        return 0xFF;
    }

    if (flashWritePending && offset == flashWriteAddr) {
        flashWritePending = false;
        return flashWriteVal ^ 0x80;
    }

    if (flashErasePending && (offset & ~0xFFF) == flashEraseSectorStart) {
        flashErasePollCount++;
        if (flashErasePollCount < 4) {
            return (flashErasePollCount & 1) ? 0x40 : 0x00;
        }
        else {
            flashErasePending = false;
            return 0xFF;
        }
    }

    return (offset < saveData.size()) ? saveData[offset] : 0xFF;
}

void SaveController::writeFlash(uint32_t address, uint8_t value) {
    uint32_t offset = (address - 0x0E000000) + flashBank * 0x10000;
    uint32_t shortAddr = address & 0xFFFF;

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
            flashErasePending = true;
            flashEraseSectorStart = sectorStart;
            flashErasePollCount = 0;
        }
        else if (shortAddr == 0x5555) {
            flashEraseArmed = false;
            switch (value) {
            case 0x90: flashIdMode = true; break;
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