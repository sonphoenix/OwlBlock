#include "cpu.h"
#include "bus.h"
#include <fstream>
#include <iostream>
#include <iomanip>

int main() {
    Bus bus;
    CPU cpu(bus);

    bus.loadBIOS("gba_bios.bin");
    bus.loadROM("bios.gba");

    cpu.skipBIOS();
    cpu.reg[15] = 0x08000000;

    std::ofstream log("log.txt");

    uint32_t lastR12 = 0;
    uint32_t lastPC = 0;

    int samePCCount = 0;

    std::cout << "Starting THUMB test ROM\n";

    for (int i = 0; i < 2000; i++) {
        bool thumb = cpu.cpsr & (1 << 5);
        uint32_t addr = thumb ? (cpu.reg[15] & ~1) : (cpu.reg[15] & ~3);
        uint32_t instr = thumb ? bus.read16(addr) : bus.read32(addr);

        // Logging
        log << std::dec << i
            << " PC=0x" << std::hex << std::setw(8) << std::setfill('0') << cpu.reg[15]
            << " instr=0x" << std::setw(thumb ? 4 : 8) << instr
            << (thumb ? " [T]" : " [A]")
            << " R7=0x" << std::setw(8) << cpu.reg[7]
            << " R1=0x" << std::setw(8) << cpu.reg[1]
            << " R2=0x" << std::setw(8) << cpu.reg[2]
            << " R6=0x" << std::setw(8) << cpu.reg[6]
            << " CPSR=0x" << std::setw(8) << cpu.cpsr
            << "\n";

        // Detect R7 changes (test progress / failure)
        if (cpu.reg[12] != lastR12) {
            std::cout << "Step " << std::dec << i
                << ": R12 = " << cpu.reg[12] << "\n";

            lastR12 = cpu.reg[12];
        }

        // Detect infinite loop (test finished)
        if (cpu.reg[15] == lastPC) {
            samePCCount++;
        }
        else {
            samePCCount = 0;
            lastPC = cpu.reg[15];
        }

        if (samePCCount > 10000) {
            std::cout << "\n=== TEST FINISHED ===\n";

            if (cpu.reg[7] == 0)
                std::cout << "PASS\n";
            else
                std::cout << "FAIL at test " << cpu.reg[7] << "\n";

            break;
        }

        cpu.Step();
        bus.tick();
    }

    log.close();

    std::cout << "\nFinal R12 = " << std::dec << cpu.reg[12] << "\n";
    std::cout << "Final PC = 0x" << std::hex << cpu.reg[15] << "\n";

    return 0;
}