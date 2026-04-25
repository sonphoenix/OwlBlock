#include "cpu.h"
#include "bus.h"

int main() {
    Bus bus;
    bus.loadBIOS("gba_bios.bin");
    std::cout << std::hex << (int)bus.bios[0] << "\n";
    //bus.loadROM("game.gba");
    CPU cpu(bus);
    cpu.reg[15] = 0x00000000;
    cpu.cpsr = MODE_SUPERVISOR;

    for (int i = 0; i <= 500; i++) {
        uint32_t instr = bus.read32(cpu.reg[15]);
        std::cout << "Step " << i
            << " PC=0x" << std::hex << cpu.reg[15]
            << " instr=0x" << instr
            << " CPSR=0x" << cpu.cpsr << "\n";
        cpu.Step();
    }
}