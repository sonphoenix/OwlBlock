#pragma once
#include <cstdint>
#include <vector>

struct SaveController {
    enum SaveType { SAVE_NONE, SAVE_SRAM, SAVE_EEPROM, SAVE_FLASH64, SAVE_FLASH128 };
    SaveType saveType = SAVE_NONE;
    std::vector<uint8_t> saveData;

    void detectSaveType(const std::vector<uint8_t>& rom);
    void detectEEPROMSize(const std::vector<uint8_t>& rom);
    void saveToDisk(const char* path);
    void loadFromDisk(const char* path);

    // EEPROM state machine
    enum EEPROMState {
        EEPROM_IDLE,
        EEPROM_REQUEST,
        EEPROM_ADDRESS,
        EEPROM_WRITE_DATA,
        EEPROM_WRITE_STOP,
        EEPROM_READ_DUMMY,
        EEPROM_READ_DATA
    };
    EEPROMState eepromState = EEPROM_IDLE;
    uint16_t eepromAddress = 0;
    int eepromBitsLeft = 0;
    uint64_t eepromBuffer = 0;
    uint8_t eepromRequestType = 0;
    bool eepromLargeAddress = false;
    uint8_t eepromReadBuffer[8] = {};
    int eepromReadBit = 0;
    bool eepromWriteDone = false;
    uint8_t readEEPROM();
    void writeEEPROM(uint8_t bit);

    // Flash state machine
    enum FlashState {
        FLASH_READ,
        FLASH_CMD1,
        FLASH_CMD2,
        FLASH_ID,
        FLASH_WRITE,
        FLASH_ERASE_CMD,
        FLASH_BANK
    };
    FlashState flashState = FLASH_READ;
    uint8_t flashBank = 0;
    bool flashIdMode = false;
    bool flashWritePending = false;
    uint32_t flashWriteAddr = 0;
    uint8_t flashWriteVal = 0;
    bool flashErasePending = false;
    uint32_t flashEraseSectorStart = 0;
    int flashErasePollCount = 0;
    bool flashEraseArmed = false;
    void writeFlash(uint32_t address, uint8_t value);
    uint8_t readFlash(uint32_t address);
};