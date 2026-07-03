#pragma once
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
struct CPU;
struct PPU;
class APU;

struct DMAchannel {
    uint32_t sad = 0, dad = 0;
    uint32_t cnt = 0;
    uint32_t isad = 0;
    uint32_t idad = 0;
};

struct Bus {

    CPU* cpuptr = nullptr;
    PPU* ppuptr = nullptr;
    APU* apuptr = nullptr;
    //memory data register 
    uint32_t mdr = 0;

    DMAchannel dma[4];
    uint32_t bios_latch = 0;
    uint8_t postflg = 0;
    uint8_t vcount =0;      // current scanline (0-227)
    uint32_t step_counter=0; // simple step counter
    uint16_t prev_irq_signal = 0; // Member variable to track IRQ state
    int frameCount = 0;
    uint16_t win0h_scanline[160] = {};
    uint16_t win0v_scanline[160] = {};
    uint16_t timer_reload[4] = {};
    uint32_t timer_ticks[4] = { 0, 0, 0, 0 };
    std::vector<uint8_t> bios ;
    std::vector<uint8_t> ewram ;
    std::vector<uint8_t> iwram ;
    std::vector<uint8_t> pram ;
    std::vector<uint8_t> vram ;
    std::vector<uint8_t> rom;
    std::vector<uint8_t> io ;
    std::vector<uint8_t> oam ;
    enum SaveType { SAVE_NONE, SAVE_SRAM, SAVE_EEPROM, SAVE_FLASH64, SAVE_FLASH128 };
    SaveType saveType = SAVE_NONE;
    std::vector<uint8_t> saveData;
    void detectSaveType();
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
    void detectEEPROMSize();

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

    Bus();
    uint8_t  read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    void tick();
    void checkIRQ();
    void setCPU(CPU* _cpu);
    void updateBiosLatch(uint32_t inst);
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);
    void loadBIOS(const char* path);
    void loadROM(const char* path);
    void writeDMA(uint32_t address,uint8_t value);
    void executeDMA(int channel);
    void setKeyState(int bit, bool pressed);
    void onVBlank();
    void onHBlank();
};