#define _USE_MATH_DEFINES
#include <cmath>
#include "core/cpu.h"
#include "common/gba_registers.h"
#include <iomanip>

#if GBA_DEBUG
#define DLOG(x) do { dbg << x; dbg.flush(); } while(0)
#else
#define DLOG(x) do {} while(0)
#endif


extern std::ofstream dbg;

void CPU::setCPSR(uint32_t val) {
    uint8_t mode = val & 0x1F;
    if (mode != 0x10 && mode != 0x11 && mode != 0x12 &&
        mode != 0x13 && mode != 0x17 && mode != 0x1B && mode != 0x1F) {
        dbg << "[BAD_CPSR] val=0x" << std::hex << val
            << " PC=0x" << reg[15] << "\n";
        dbg.flush();
    }
    cpsr = val;
}

void CPU::switchMode(uint8_t newMode) {
    uint8_t oldMode = cpsr & 0x1F;
    /*dbg << "[switchMode] " << std::hex << (int)oldMode << "->" << (int)newMode
            << " spsr_svc=0x" << spsr_svc
        << " reg[13]=0x" << reg[13] << " reg[14]=0x" << reg[14] << "\n";
    dbg.flush();*/
    // -----------------
    // SAVE CURRENT BANK
    // -----------------

    if (oldMode != MODE_FIQ && newMode == MODE_FIQ) {
        // Entering FIQ: save user R8-R12 before FIQ bank takes over
        for (int i = 0; i < 5; i++)
            reg_user[i] = reg[8 + i];
    }

    if (oldMode == MODE_FIQ) {
        // Leaving FIQ: save FIQ R8-R12
        for (int i = 0; i < 5; i++)
            reg_fiq[i] = reg[8 + i];
    }

    switch (oldMode) {
    case MODE_FIQ:
        reg_fiq[5] = reg[13];
        reg_fiq[6] = reg[14];
        break;
    case MODE_IRQ:
        reg_irq[0] = reg[13];
        reg_irq[1] = reg[14];
        break;
    case MODE_SUPERVISOR:
        reg_svc[0] = reg[13];
        reg_svc[1] = reg[14];
        break;
    case MODE_ABORT:
        reg_abt[0] = reg[13];
        reg_abt[1] = reg[14];
        break;
    case MODE_UNDEFINED:
        reg_und[0] = reg[13];
        reg_und[1] = reg[14];
        break;
    default:
        // USER or SYSTEM: reg[13]/reg[14] are the user-bank regs.
        reg_user[5] = reg[13];
        reg_user[6] = reg[14];
        break;
    }

    // -----------------
    // RESTORE NEW BANK
    // -----------------

    if (newMode == MODE_FIQ) {
        for (int i = 0; i < 5; i++)
            reg[8 + i] = reg_fiq[i];
    }
    else if (oldMode == MODE_FIQ) {
        // Leaving FIQ: restore user R8-R12
        for (int i = 0; i < 5; i++)
            reg[8 + i] = reg_user[i];
    }

    switch (newMode) {
    case MODE_FIQ:
        reg[13] = reg_fiq[5];
        reg[14] = reg_fiq[6];
        break;
    case MODE_IRQ:
        reg[13] = reg_irq[0];
        reg[14] = reg_irq[1];
        break;
    case MODE_SUPERVISOR:
        reg[13] = reg_svc[0];
        reg[14] = reg_svc[1];
        break;
    case MODE_ABORT:
        reg[13] = reg_abt[0];
        reg[14] = reg_abt[1];
        break;
    case MODE_UNDEFINED:
        reg[13] = reg_und[0];
        reg[14] = reg_und[1];
        break;
    default:
        // USER or SYSTEM
        reg[13] = reg_user[5];
        reg[14] = reg_user[6];
        break;
    }
}



void CPU::restoreCPSRFromSPSR() {
    uint8_t oldMode = cpsr & 0x1F;
    uint32_t spsr = 0;
    switch (oldMode) {
    case MODE_FIQ:        spsr = spsr_fiq; break;
    case MODE_IRQ:        spsr = spsr_irq; break;
    case MODE_SUPERVISOR: spsr = spsr_svc; break;
    case MODE_ABORT:      spsr = spsr_abt; break;
    case MODE_UNDEFINED:  spsr = spsr_und; break;
    default:
        dbg << "[CPSR_RESTORE_ABORT_NO_SPSR]\n";
        return;
    }
    uint8_t newMode = spsr & 0x1F;
    bool valid = (newMode == 0x10 || newMode == 0x11 || newMode == 0x12 ||
        newMode == 0x13 || newMode == 0x17 || newMode == 0x1B ||
        newMode == 0x1F);
    if (!valid) {
        dbg << "[RESTORE_BAD_SPSR] spsr=0x" << std::hex << spsr
            << " oldMode=0x" << (int)oldMode
            << " PC=0x" << reg[15] << "\n";
        dbg.flush();
        return;
    }
    dbg << "[RESTORE_CPSR] oldMode=0x" << std::hex << (int)oldMode
        << " newMode=0x" << (int)newMode
        << " R0_before=0x" << reg[0] << "\n";
    dbg.flush();
    if (newMode != oldMode)
        switchMode(newMode);
    cpsr = spsr;
    dbg << "[RESTORE_CPSR_AFTER] R0_after=0x" << std::hex << reg[0] << "\n";
    dbg.flush();
}

void CPU::skipBIOS() {
    // Clear WRAM stacks and BIOS IRQ vector/flags
    for (uint32_t i = 0x3007E00; i < 0x3008000; i++) {
        bus.write8(i, 0);
    }

    // Clear R0-R12
    for (int i = 0; i < 13; i++) reg[i] = 0;

    // Clear banked registers
    reg_svc[0] = 0x03007FE0;  // SP_svc
    reg_svc[1] = 0x00000000;  // LR_svc
    reg_irq[0] = 0x03007FA0;  // SP_irq
    reg_irq[1] = 0x00000000;  // LR_irq
    spsr_svc = 0x00000000;
    spsr_irq = 0x00000000;

    // System mode, ARM, IRQ/FIQ enabled
    cpsr = 0x0000001F;
    reg[13] = 0x03007F00;  // SP_usr
    reg[14] = 0x00000000;  // LR_usr
    reg[15] = 0x08000000;  // jump to ROM

    // Post-BIOS IO state
    bus.io[0x00] = 0x00; bus.io[0x01] = 0x00;  // DISPCNT
    bus.io[0x04] = 0x00; bus.io[0x05] = 0x00;  // DISPSTAT
    bus.io[0x06] = 0x00; bus.io[0x07] = 0x00;  // VCOUNT
    bus.io[0x88] = 0x00; bus.io[0x89] = 0x02;  // SOUNDBIAS
    bus.io[0x130] = 0xFF; bus.io[0x131] = 0x03; // KEYINPUT

    // Serial — prevents fake link cable detection
    bus.io[0x128] = 0x00; bus.io[0x129] = 0x00; // SIOCNT
    bus.io[0x134] = 0x80; bus.io[0x135] = 0x80; // RCNT

    // BIOS interrupt state
    bus.write32(0x03FFFFF8, 0x00000000);
    bus.write32(0x03FFFFFC, 0x00000000);
    for (int i = 0; i < 8; i++) bus.iwram[0x7FF8 + i] = 0;

    bus.postflg = 1;
    cpsr &= ~(1 << 5); // ensure ARM mode
    
}

int CPU::Step() {
    static uint32_t stepCounter = 0;
    stepCounter++;

   /* dbg << "[STEP " << std::dec << stepCounter << "] PC=0x" << std::hex << reg[15]
        << " mode=0x" << (cpsr & 0x1F)
        << " T=" << ((cpsr >> 5) & 1)
        << " I=" << ((cpsr >> 7) & 1)
        << " IME=" << (bus.io[0x208] | (bus.io[0x209] << 8))
        << " IE=" << (bus.io[0x200] | (bus.io[0x201] << 8))
        << " IF=" << (bus.io[0x202] | (bus.io[0x203] << 8))
        << " VCOUNT=" << std::dec << (int)bus.vcount
        << " DISPSTAT=0x" << std::hex << (int)bus.io[0x04]
        << " halted=" << halted << "\n";*/

    if (reg[15] >= 0xFF000000) {
        dbg << "[CRASH] PC went to 0x" << std::hex << reg[15]
            << " LR=0x" << reg[14]
            << " SP=0x" << reg[13]
            << " CPSR=0x" << cpsr << "\n";
        dbg.flush();
    }
    if (halted) return 1;

    static uint32_t last_pc = 0;
    static int same_pc_count = 0;
    if (reg[15] == last_pc) {
        if (++same_pc_count == 2000000) {
            DLOG("[STUCK] PC=0x" << std::hex << reg[15]
                << " LR=0x" << reg[14] << "\n");
            same_pc_count = 0;
        }
    }
    else {
        same_pc_count = 0;
        last_pc = reg[15];
    }

    uint32_t prev_pc = reg[15];

    /*if (prev_pc == 0x128) {
        dbg << "[AT_128] R0=0x" << std::hex << reg[0] << "\n";
        dbg.flush();
    }
    if ((cpsr & 0x1F) == 0x12) {  // IRQ mode
        dbg << "[IRQ_MODE_STEP] PC=0x" << std::hex << prev_pc
            << " SP=0x" << reg[13]
            << " R0=0x" << reg[0] << "\n";
        dbg.flush();
    }
    if (prev_pc >= 0x08000394 && prev_pc <= 0x0800039A) {
        dbg << "[LOOP_STEP] PC=0x" << std::hex << prev_pc
            << " R0=0x" << reg[0]
            << " SP=0x" << reg[13]
            << " mode=0x" << (cpsr & 0x1F) << "\n";
        dbg.flush();
    }*/
    static uint32_t entry_last_pc = 0xFFFFFFFF;
    if (prev_pc == 0x168 || prev_pc == 0x16C || prev_pc == 0x170) {
        dbg << "[BIOS_PRE_DIV] PC=0x" << std::hex << prev_pc
            << " R0=0x" << reg[0]
            << " R1=0x" << reg[1]
            << " LR=0x" << reg[14] << "\n";
        dbg.flush();
    }
    entry_last_pc = prev_pc;
    if (cpsr & (1 << 5)) {
        uint16_t instr = bus.read16(prev_pc);
        if (prev_pc < 0x4000)
            bus.updateBiosLatch(bus.read32(prev_pc & ~3));
        reg[15] += 2;
        return executeThumb(instr);
    }
    else {
        uint32_t instr = bus.read32(prev_pc);
        if (prev_pc < 0x4000)
            bus.updateBiosLatch(instr);
        reg[15] += 4;
        return Execute(instr);
    }
}


void CPU::reset() {
    for (int i = 0; i < 16; i++) reg[i] = 0;
    cpsr = 0xD3;
    halted = false;
    reg_svc[0] = reg_svc[1] = 0;
    reg_irq[0] = reg_irq[1] = 0;
    reg_abt[0] = reg_abt[1] = 0;
    reg_und[0] = reg_und[1] = 0;
    for (int i = 0; i < 7; i++) { reg_fiq[i] = 0; reg_user[i] = 0; }
    spsr_svc = spsr_irq = spsr_abt = spsr_und = spsr_fiq = 0;
    skipBIOS();   // re-establish boot PC/SP exactly like the initial startup does
}

uint32_t CPU::getReg(uint8_t index) {
    if (index == 15) {
        if (cpsr & (1 << 5))   // Thumb
            return (reg[15] ) + 2;   // word‑aligned PC + 4
        else                   // ARM
            return reg[15] + 4;          // already at current_pc+4 → add 4 more
    }
    return reg[index];
}

bool const CPU::checkCondition(uint32_t cond) {
    bool N = (cpsr >> 31) & 1;
    bool Z = (cpsr >> 30) & 1;
    bool C = (cpsr >> 29) & 1;
    bool V = (cpsr >> 28) & 1;

    switch (cond) {
    case 0b0000: return Z;           //EQ
    case 0b0001: return !Z;          //NQ
    case 0b0010: return C;           //CS
    case 0b0011: return !C;          //CC
    case 0b0100: return N;           //MI
    case 0b0101: return !N;          //PL
    case 0b0110: return V;           //VS
    case 0b0111: return !V;          //VC
    case 0b1000: return C && !Z;     //HI
    case 0b1001: return Z || !C;     //LS
    case 0b1010: return N == V;      //GE
    case 0b1011: return N != V;      //LT
    case 0b1100: return !Z && N == V;// GT
    case 0b1101: return Z || N != V; // LE
    case 0b1110: return true;        // AL always
    case 0b1111: return false; // NV — never execute on ARM7TDMI
    default:     return true;
    }
}

void CPU::executeBranch(uint32_t instruction) {
    bool link = (instruction >> 24) & 1;
    int32_t offset = instruction & 0xFFFFFF;
    if (offset & 0x800000) offset |= 0xFF000000;
    uint32_t pc = reg[15] + 4;

    /*dbg << "[BRANCH] reg15_before=0x" << std::hex << reg[15]
        << " offset_raw=0x" << (instruction & 0xFFFFFF)
        << " offset_signed=0x" << offset
        << " pc_calc=0x" << pc
        << " target=0x" << (pc + (offset << 2)) << "\n";
    dbg.flush();*/

    if (link) {
        reg[14] = pc - 4;
    }
    reg[15] = pc + (offset << 2);
}

void CPU::executeBx(uint32_t instruction) {
    uint8_t Rn = instruction & 0xF;
    uint32_t target = getReg(Rn); 

    if (target & 1) {
        cpsr |= (1u << 5);        // THUMB
        reg[15] = target & ~1;
    }
    else {
        cpsr &= ~(1u << 5);       // ARM
        reg[15] = target & ~3;
    }
}

uint32_t CPU::rotate_right(uint32_t value, uint8_t ammount) {
    if (ammount == 0) return value;
    return (value >> ammount) | (value << (32 - ammount));
}

uint32_t CPU::apply_shift(uint32_t value, uint8_t shift) {
    uint8_t type = (shift >> 1) & 0x3;
    uint8_t how = shift & 1;
    uint8_t amount;

    if (how == 0) {
        // Immediate shift
        amount = (shift >> 3) & 0x1F;
        if (amount == 0) {
            switch (type) {
            case 0b00: return value;  // LSL #0 — no-op
            case 0b01: // LSR #0 means LSR #32
                ((value >> 31) & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
                return 0;
            case 0b10: // ASR #0 means ASR #32
                ((value >> 31) & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
                return (uint32_t)((int32_t)value >> 31);
            case 0b11: { // ROR #0 means RRX
                uint32_t carry_in = (cpsr >> 29) & 1;
                (value & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
                return (value >> 1) | (carry_in << 31);
            }
            }
        }
    }
    else {
        // Register shift
        uint8_t Rs = (shift >> 4) & 0xF;
        // Rs=PC: ARM spec says use instr_addr+8 = reg[15]-4
        // (reg[15] during execute = base_addr+12 = instr_addr+12)
        uint32_t rsVal = (Rs == 15) ? (reg[15] - 4) : reg[Rs];
        amount = rsVal & 0xFF;
        if (amount == 0) return value; // carry unchanged

        if (amount >= 32) {
            switch (type) {
            case 0b00: // LSL
                (amount == 32 && (value & 1)) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
                return 0;
            case 0b01: // LSR
                (amount == 32 && ((value >> 31) & 1)) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
                return 0;
            case 0b10: // ASR
                ((value >> 31) & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
                return (uint32_t)((int32_t)value >> 31);
            case 0b11: // ROR — wraps mod 32
                amount %= 32;
                if (amount == 0) {
                    ((value >> 31) & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
                    return value;
                }
                break;
            }
        }
    }

    // Normal shift (amount 1-31)
    switch (type) {
    case 0b00: // LSL
        ((value >> (32 - amount)) & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
        return value << amount;
    case 0b01: // LSR
        ((value >> (amount - 1)) & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
        return value >> amount;
    case 0b10: // ASR
        ((value >> (amount - 1)) & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
        return (uint32_t)((int32_t)value >> amount);
    case 0b11: // ROR
        ((value >> (amount - 1)) & 1) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
        return rotate_right(value, amount);
    }
    return value;
}

void CPU::setFlags(uint32_t result, uint64_t full, uint32_t Rn, uint32_t op2, bool isSub) {
    // N (Negative) and Z (Zero)
    (result >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31));
    (result == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));

    if (isSub) {
        // C = NOT borrow → check 33rd bit
        bool noBorrow = !(full >> 32);
        noBorrow ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));

        // V = overflow
        bool v = ((Rn ^ op2) & (Rn ^ result)) >> 31;
        v ? (cpsr |= (1u << 28)) : (cpsr &= ~(1u << 28));
    }
    else {
        // C = 1 if carry occurred (Result > 0xFFFFFFFF)
        bool carry = (full >> 32) & 1;
        carry ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));

        // V = Signed Overflow
        bool v = ((~(Rn ^ op2) & (Rn ^ result)) >> 31);
        v ? (cpsr |= (1u << 28)) : (cpsr &= ~(1u << 28));
    }
}

void CPU::executeDataProcessing(uint32_t instruction) {
    uint32_t oldCpsr = cpsr;
    uint8_t immediate = (instruction >> 25) & 1;
    uint8_t opcode = (instruction >> 21) & 0xF;
    uint8_t Rd = (instruction >> 12) & 0xF;
    uint8_t S = (instruction >> 20) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;
    bool op2_is_reg_shift = !immediate && ((instruction >> 4) & 1);
    auto getOp2 = [&](uint8_t RnIndex) -> uint32_t {
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            uint32_t res = rotate_right(imm8, rotation * 2);
            if (rotation != 0 && S) {
                (res >> 31) ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
            }
            return res;
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift_field = (instruction >> 4) & 0xFF;
            bool is_reg_shift = (shift_field & 1);
            uint32_t rm_val;
            if (rm == 15) {
                rm_val = reg[15] + (is_reg_shift ? 8 : 4);
            }
            else {
                rm_val = getReg(rm);
            }
            return apply_shift(rm_val, shift_field);
        }
        };
    uint32_t rnVal;
    if (Rn == 15 && op2_is_reg_shift) {
        rnVal = getReg(15) + 4;
    }
    else {
        rnVal = getReg(Rn);
    }
    uint32_t carryIn = (cpsr >> 29) & 1;
    uint32_t op2 = getOp2(Rn);
    uint32_t result = 0;
    uint64_t full = 0;
    bool isArithmetic = false;
    bool isSubtract = false;
    switch (opcode) {
    case 0b0000: result = rnVal & op2; break;
    case 0b0001: result = rnVal ^ op2; break;
    case 0b0010: full = (uint64_t)rnVal - op2; isArithmetic = true; isSubtract = true; break;
    case 0b0011: full = (uint64_t)op2 - rnVal; isArithmetic = true; isSubtract = true; break;
    case 0b0100: full = (uint64_t)rnVal + op2; isArithmetic = true; break;
    case 0b0101: full = (uint64_t)rnVal + op2 + carryIn; isArithmetic = true; break;
    case 0b0110: full = (uint64_t)rnVal - op2 + carryIn - 1; isArithmetic = true; isSubtract = true; break;
    case 0b0111: full = (uint64_t)op2 - rnVal + carryIn - 1; isArithmetic = true; isSubtract = true; break;
    case 0b1000: result = rnVal & op2; break;
    case 0b1001: result = rnVal ^ op2; break;
    case 0b1010: full = (uint64_t)rnVal - op2; isArithmetic = true; isSubtract = true; break;
    case 0b1011: full = (uint64_t)rnVal + op2; isArithmetic = true; break;
    case 0b1100: result = rnVal | op2; break;
    case 0b1101: result = op2; break;
    case 0b1110: result = rnVal & ~op2; break;
    case 0b1111: result = ~op2; break;
    }
    if (isArithmetic) result = (uint32_t)full;
    bool writesResult = (opcode < 0b1000 || opcode > 0b1011);
    if (Rd == 15) {
        if (S) {
            uint8_t mode = cpsr & 0x1F;
            if (mode != MODE_USER && mode != MODE_SYSTEM) {
                restoreCPSRFromSPSR();
            }
        }
        if (writesResult) {
            if (cpsr & (1 << 5))
                reg[15] = result & ~1;
            else
                reg[15] = result & ~3;
            return;
        }
    }
    else {
        if (writesResult) {
            reg[Rd] = result;
          }
  }
    if (S) {
        /*dbg << "[SUBS_PC_LR] result=0x" << std::hex << result
            << " R0=0x" << reg[0]
            << " mode_before=0x" << (int)(oldCpsr & 0x1F) << "\n";
        dbg.flush();*/
        uint32_t beforeFlags = cpsr;
        if (isArithmetic) {
            setFlags(result, full, rnVal, op2, isSubtract);
        }
        else {
            (result >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31));
            (result == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));
        }
    }
}

void CPU::executeMultiply(uint32_t instruction) {
    uint8_t A = (instruction >> 21) & 1;
    uint8_t S = (instruction >> 20) & 1;
    uint8_t Rd = (instruction >> 16) & 0xF;
    uint8_t Rn = (instruction >> 12) & 0xF;
    uint8_t Rs = (instruction >> 8) & 0xF;
    uint8_t Rm = instruction & 0xF;

    if (A) { // MLA
        reg[Rd] = getReg(Rm) * getReg(Rs) + getReg(Rn);
    }
    else  // MUL
        reg[Rd] = getReg(Rm) * getReg(Rs);

    if (S) { // only N and Z matter here
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31));  // N
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));  // Z
    }
}

void CPU::executeMultiplyLong(uint32_t instruction) {
    uint8_t U = (instruction >> 22) & 1; // 1=signed, 0=unsigned
    uint8_t A = (instruction >> 21) & 1; // 1=accumulate, 0=don't
    uint8_t S = (instruction >> 20) & 1;
    uint8_t RdHi = (instruction >> 16) & 0xF;
    uint8_t RdLo = (instruction >> 12) & 0xF;
    uint8_t Rs = (instruction >> 8) & 0xF;
    uint8_t Rm = instruction & 0xF;

    uint64_t result;

    if (U) { // signed
        int64_t sresult = (int64_t)(int32_t)getReg(Rm) * (int64_t)(int32_t)getReg(Rs);
        if (A) {
            int64_t acc = ((int64_t)getReg(RdHi) << 32) | (uint64_t)getReg(RdLo);
            sresult += acc;
        }
        result = sresult;
    }
    else { // unsigned
        uint64_t uresult = (uint64_t)getReg(Rm) * (uint64_t)getReg(Rs);
        if (A) {
            uint64_t acc = ((uint64_t)getReg(RdHi) << 32) | getReg(RdLo);
            uresult += acc;
        }
        result = uresult;
    }

    reg[RdHi] = (result >> 32);
    reg[RdLo] = result & 0xFFFFFFFF;

    if (S) {
        (result >> 63) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31));  // N
        (result == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));  // Z
    }
}

void CPU::executeLoadStore(uint32_t instruction) {
    uint8_t I = (instruction >> 25) & 1;
    uint8_t P = (instruction >> 24) & 1;
    uint8_t U = (instruction >> 23) & 1;
    uint8_t B = (instruction >> 22) & 1;
    uint8_t W = (instruction >> 21) & 1;
    uint8_t L = (instruction >> 20) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;
    uint8_t Rd = (instruction >> 12) & 0xF;

    uint32_t offset;
    if (I == 0) {
        offset = instruction & 0xFFF;
    }
    else {
        uint32_t savedCpsr = cpsr;
        uint8_t Rm = instruction & 0xF;
        uint8_t shift = (instruction >> 4) & 0xFF;
        offset = apply_shift(getReg(Rm), shift);
        cpsr = savedCpsr;
    }

    uint32_t base = (Rn == 15) ? ((reg[15] + 4) & ~3) : getReg(Rn);
    uint32_t address = U ? (base + offset) : (base - offset);
    uint32_t effective = P ? address : base;

    if (L) {
        if (B) {
            reg[Rd] = bus.read8(effective);
        }
        else {
            uint32_t value = bus.read32(effective & ~3);
            uint8_t rotation = (effective & 3) * 8;
            reg[Rd] = rotate_right(value, rotation);
        }

    }
    else {
        if (B) {
            bus.write8(effective, reg[Rd] & 0xFF);
        }
        else {
            if (Rd == 15) {
                bus.write32(effective & ~3, reg[Rd] + 8);
                return;
            }
            bus.write32(effective & ~3, reg[Rd]);
        }
    }

    if (!P || W) {
        if (!L || (Rn != Rd)) {
            reg[Rn] = address;
        }
    }
}


void CPU::executeHalfwordTransfer(uint32_t instruction) {
    uint8_t P = (instruction >> 24) & 1;
    uint8_t U = (instruction >> 23) & 1;
    uint8_t I = (instruction >> 22) & 1;
    uint8_t W = (instruction >> 21) & 1;
    uint8_t L = (instruction >> 20) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;
    uint8_t Rd = (instruction >> 12) & 0xF;
    uint8_t SH = (instruction >> 5) & 0x3;

    uint32_t offset;
    if (I) {
        uint8_t immHi = (instruction >> 8) & 0xF;
        uint8_t immLo = instruction & 0xF;
        offset = (immHi << 4) | immLo;
    }
    else {
        offset = getReg(instruction & 0xF);
    }

    uint32_t base = getReg(Rn);
    uint32_t address = U ? (base + offset) : (base - offset);
    uint32_t effective = P ? address : base;

    if (L) { // LOAD
        switch (SH) {
        case 0b01: { // LDRH
            uint32_t val = bus.read16(effective & ~1);  // read halfword aligned
            uint8_t rotation = (effective & 1) * 8;     // 0 or 8 bits
            reg[Rd] = rotate_right(val, rotation);      // no & 0xFFFF
            break;
        }

        case 0b10: { // LDRSB (Signed Byte)
            uint32_t val = bus.read8(effective);
            if (val & 0x80) val |= 0xFFFFFF00;
            reg[Rd] = val;
            break;
        }
        case 0b11: { // LDRSH (Signed Halfword)
            if (effective & 1) {
                // ARM7TDMI Specific: Misaligned LDRSH acts as LDRSB
                uint32_t val = bus.read8(effective);
                if (val & 0x80) val |= 0xFFFFFF00;
                reg[Rd] = val;
            }
            else {
                uint32_t val = bus.read16(effective & ~1);
                if (val & 0x8000) val |= 0xFFFF0000;
                reg[Rd] = val;
            }
            break;
        }
        }
        if (Rd == 15) reg[15] &= ~3;
    }
    else { // STORE
        if (SH == 0b01) { // STRH
            uint32_t val = reg[Rd];
            if (Rd == 15) val += 8; // Pipeline offset for PC
            bus.write16(effective & ~1, val & 0xFFFF);
        }
    }

    if (!P || W) {
        if (!L || (Rn != Rd)) {
            reg[Rn] = address;
        }
    }
}

void CPU::executeSWP(uint32_t instruction) {
    uint8_t B = (instruction >> 22) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;  // address
    uint8_t Rd = (instruction >> 12) & 0xF;  // destination
    uint8_t Rm = instruction & 0xF;           // source

    uint32_t base = getReg(Rn);  // Fixed: use getReg for PC+8

    if (B) {
        // SWPB — byte
        uint8_t temp = bus.read8(base);
        bus.write8(base, getReg(Rm) & 0xFF);  // Fixed: use getReg for Rm
        reg[Rd] = temp;
    }
    else {
        // SWP — word
        uint32_t temp = bus.read32(base);
        bus.write32(base& ~3, getReg(Rm));  // Fixed: use getReg for Rm
        reg[Rd] = temp;
    }

    // Safety: if Rd or Rm is PC, enforce alignment
    if (Rd == 15) reg[15] &= ~3;
    if (Rm == 15) reg[15] &= ~3;
}

void CPU::executeLoadStoreMultiple(uint32_t instruction) {
    uint8_t P = (instruction >> 24) & 1;
    uint8_t U = (instruction >> 23) & 1;
    uint8_t S = (instruction >> 22) & 1;
    uint8_t W = (instruction >> 21) & 1;
    uint8_t L = (instruction >> 20) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;
    uint16_t regList = instruction & 0xFFFF;
    uint32_t base = reg[Rn];

    if (regList == 0) {
        uint32_t addr = base;
        if (!U) addr -= 64;
        if (P == U) addr += 4;
        if (L) {
            reg[15] = bus.read32(addr & ~3) & ~3;
        }
        else {
            bus.write32(addr & ~3, reg[15] + 8);
        }
        if (W) reg[Rn] = U ? (base + 64) : (base - 64);
        return;
    }

    int count = 0;
    for (int i = 0; i < 16; i++) if (regList & (1 << i)) count++;

    int lowestReg = -1;
    for (int i = 0; i < 16; i++) {
        if (regList & (1 << i)) { lowestReg = i; break; }
    }

    uint32_t writebackVal = U ? (base + count * 4) : (base - count * 4);
    uint32_t address = base;
    if (!U) address -= (count * 4);
    if (P == U) address += 4;

    for (int i = 0; i < 16; i++) {
        if (regList & (1 << i)) {
            if (L) {

                uint32_t val = bus.read32(address & ~3);
                /*dbg << "[LDM_LOAD] reg=" << i << " addr=0x" << std::hex << address
                    << " val=0x" << val << "\n";
                dbg.flush();*/
                if (S && !(regList & (1 << 15))) {
                    if (i >= 8 && i <= 12) reg_user[i - 8] = val;
                    else if (i == 13)      reg_user[5] = val;
                    else if (i == 14)      reg_user[6] = val;
                    else                   reg[i] = val;
                }
                else {
                    reg[i] = val;
                    if (i == 15) reg[15] &= ~3;
                }
            }
            else {
                uint32_t val;
                if (S) {
                  /*  dbg << "[LDM_S] L=" << (int)L << " PC_in_list=" << ((regList >> 15) & 1)
                        << " Rn=" << (int)Rn << " list=0x" << std::hex << regList
                        << " base=0x" << base
                        << " PC=0x" << reg[15] << "\n";
                    dbg.flush();*/
                    val = (i >= 8 && i <= 12) ? reg_user[i - 8] :
                        (i == 13) ? reg_user[5] :
                        (i == 14) ? reg_user[6] : reg[i];
                }
                else if (i == 15) {
                    val = reg[15] + 8;
                }
                else if (W && i == Rn && i != lowestReg) {
                    val = writebackVal;
                }
                else {
                    val = reg[i];
                }
                bus.write32(address & ~3, val);
            }
            address += 4;
        }
    }

    if (W && !(L && (regList & (1 << Rn)))) {
        reg[Rn] = writebackVal;
    }
    if (L && S && (regList & (1 << 15))) {
        dbg << "executeLoadStoreMultiple restoreCPSRFromSPSR saaar\n";
        dbg.flush();
        restoreCPSRFromSPSR();
    }
}

void CPU::executePSRTransfer(uint32_t instruction) {
    uint8_t Pd = (instruction >> 22) & 1;
    uint8_t op = (instruction >> 21) & 1;
    uint8_t I = (instruction >> 25) & 1;

    if (op) {
        // MSR - write to PSR
        uint32_t value;
        if (I) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm = instruction & 0xFF;
            value = rotate_right(imm, rotation * 2);
        }
        else {
            uint8_t Rm = instruction & 0xF;
            value = getReg(Rm);
        }

        uint32_t mask = 0;
        if (instruction & 0x00080000) mask |= 0xFF000000;
        if (instruction & 0x00040000) mask |= 0x00FF0000;
        if (instruction & 0x00020000) mask |= 0x0000FF00;
        if (instruction & 0x00010000) mask |= 0x000000FF;

        uint8_t mode = cpsr & 0x1F;
        if (mode == MODE_USER)
            mask &= 0xFF000000;

        if (Pd) {
            // Write SPSR
            switch (mode) {
            case MODE_IRQ:        spsr_irq = (spsr_irq & ~mask) | (value & mask); break;
            case MODE_FIQ:        spsr_fiq = (spsr_fiq & ~mask) | (value & mask); break;
            case MODE_SUPERVISOR:
                spsr_svc = (spsr_svc & ~mask) | (value & mask);
                dbg << "[MSR_SPSR_SVC] spsr_svc=0x" << std::hex << spsr_svc
                    << " mask=0x" << mask << " value=0x" << value
                    << " PC=0x" << reg[15] << "\n";
                dbg.flush();
            break;            case MODE_ABORT:      spsr_abt = (spsr_abt & ~mask) | (value & mask); break;
            case MODE_UNDEFINED:  spsr_und = (spsr_und & ~mask) | (value & mask); break;
            default: break;
            }
        }
        else {
            // Write CPSR
            uint32_t newCpsr = (cpsr & ~mask) | (value & mask);
            uint8_t oldMode = cpsr & 0x1F;
            uint8_t newMode = newCpsr & 0x1F;

            // Validate new mode if control byte is being written
            if (mask & 0xFF) {
                bool valid = (newMode == 0x10 || newMode == 0x11 ||
                    newMode == 0x12 || newMode == 0x13 ||
                    newMode == 0x17 || newMode == 0x1B ||
                    newMode == 0x1F);
                if (!valid) {
                    dbg << "[MSR_BAD_MODE] newMode=0x" << std::hex << (int)newMode
                        << " value=0x" << value
                        << " mask=0x" << mask
                        << " PC=0x" << reg[15] << "\n";
                    dbg.flush();
                    return;
                }
            }

            if (oldMode != newMode)
                switchMode(newMode);
            cpsr = newCpsr;
        }
    }
    else {
        // MRS - read PSR
        uint8_t Rd = (instruction >> 12) & 0xF;
        if (Pd) {
            uint8_t mode = cpsr & 0x1F;
            switch (mode) {
            case MODE_IRQ:        reg[Rd] = spsr_irq; break;
            case MODE_FIQ:        reg[Rd] = spsr_fiq; break;
            case MODE_SUPERVISOR: reg[Rd] = spsr_svc; break;
            case MODE_ABORT:      reg[Rd] = spsr_abt; break;
            case MODE_UNDEFINED:  reg[Rd] = spsr_und; break;
            default:              reg[Rd] = cpsr; break;
            }
        }
        else {
            reg[Rd] = cpsr;
        }

        if (Rd == 15)
            reg[15] &= ~3;
    }
}


void CPU::executeSWI(uint32_t instruction) {
    bool thumb = (cpsr >> 5) & 1;
    uint8_t swiNum = (instruction >> (thumb ? 0 : 16)) & 0xFF;

    dbg << "[SWI] num=0x" << std::hex << (int)swiNum
        << " PC=0x" << reg[15] << "\n";
    dbg.flush();

    dbg << "[SWI_RAW] instr=0x" << std::hex << instruction
        << " thumb=" << thumb << " PC=0x" << reg[15] << "\n";
    dbg.flush();

    if (swiNum == 0x06) {
        if (reg[1] == 0) {
            dbg << "[DIV_BY_ZERO] R0=0x" << std::hex << reg[0]
                << " PC=0x" << reg[15] << "\n";
            dbg.flush();
            reg[3] = reg[0];
        }
        else {
            int32_t dividend = (int32_t)reg[0];
            int32_t divisor = (int32_t)reg[1];
            int32_t result = dividend / divisor;
            int32_t remainder = dividend % divisor;
            reg[0] = (uint32_t)result;
            reg[1] = (uint32_t)remainder;
            reg[3] = (uint32_t)(result < 0 ? -result : result);
            dbg << "[DIV] R0=" << std::dec << dividend
                << " R1=" << divisor
                << " result=" << result << "\n";
            dbg.flush();
        }
        return;
    }

    if (swiNum == 0x07) { // DivArm — args swapped vs Div
        if (reg[0] == 0) {
            reg[3] = reg[1];
        }
        else {
            int32_t dividend = (int32_t)reg[1];
            int32_t divisor = (int32_t)reg[0];
            int32_t result = dividend / divisor;
            int32_t remainder = dividend % divisor;
            reg[0] = (uint32_t)result;
            reg[1] = (uint32_t)remainder;
            reg[3] = (uint32_t)(result < 0 ? -result : result);
        }
        return;
    }

    if (swiNum == 0x08) { // Sqrt
        uint32_t val = reg[0];
        reg[0] = (uint32_t)sqrtf((float)val);
        return;
    }

    if (swiNum == 0x09) { // ArcTan
        int16_t tan = (int16_t)reg[0];
        float angle = atanf(tan / 16384.0f) / (float)(M_PI / 2.0) * 16384.0f;
        reg[0] = (uint32_t)(int32_t)(int16_t)angle;
        return;
    }

    if (swiNum == 0x0A) { // ArcTan2
        int16_t x = (int16_t)reg[0];
        int16_t y = (int16_t)reg[1];
        float angle = atan2f((float)y, (float)x) / (float)M_PI * 32768.0f;
        reg[0] = (uint32_t)(int32_t)(int16_t)angle;
        return;
    }

    if (swiNum == 0x0B) {
        uint32_t src = reg[0];
        uint32_t dst = reg[1];
        uint32_t cnt = reg[2];
        uint32_t count = cnt & 0x1FFFFF;
        bool     fill = (cnt >> 24) & 1;
        bool     word = (cnt >> 26) & 1;
        uint32_t unitSize = word ? 4 : 2;
        dbg << "[CPUSET] src=0x" << std::hex << src
            << " dst=0x" << dst
            << " count=" << std::dec << count
            << " fill=" << fill
            << " word=" << word << "\n";
        dbg.flush();
        if (fill) {
            uint32_t fillVal = word ? bus.read32(src) : bus.read16(src);
            for (uint32_t i = 0; i < count; i++) {
                if (word) bus.write32(dst, fillVal);
                else      bus.write16(dst, (uint16_t)fillVal);
                dst += unitSize;
            }
        }
        else {
            for (uint32_t i = 0; i < count; i++) {
                if (word) bus.write32(dst, bus.read32(src));
                else      bus.write16(dst, bus.read16(src));
                src += unitSize;
                dst += unitSize;
            }
        }
        return;
    }

    if (swiNum == 0x0C) {
        uint32_t src = reg[0];
        uint32_t dst = reg[1];
        uint32_t cnt = reg[2];
        uint32_t count = (cnt & 0x1FFFFF);
        bool     fill = (cnt >> 24) & 1;
        count = (count + 7) & ~7;
        dbg << "[CPUFASTSET] src=0x" << std::hex << src
            << " dst=0x" << dst
            << " count=" << std::dec << count
            << " fill=" << fill << "\n";
        dbg.flush();
        if (fill) {
            uint32_t fillVal = bus.read32(src);
            for (uint32_t i = 0; i < count; i++) {
                bus.write32(dst, fillVal);
                dst += 4;
            }
        }
        else {
            for (uint32_t i = 0; i < count; i++) {
                bus.write32(dst, bus.read32(src));
                src += 4;
                dst += 4;
            }
        }
        return;
    }

    uint32_t returnAddr = thumb ? reg[15] : reg[15] - 4;
    uint32_t oldCPSR = cpsr;
    spsr_svc = oldCPSR;
    switchMode(MODE_SUPERVISOR);
    reg[14] = returnAddr;
    cpsr = (oldCPSR & ~0xBF) | MODE_SUPERVISOR | (1 << 7);
    reg[15] = 0x08;
}

void CPU::triggerIRQ() {
    dbg << "[IRQ_FIRED] IF=0x" << std::hex << (bus.io[0x202] | (bus.io[0x203] << 8)) << "\n";    dbg.flush();
    if ((cpsr & 0x1F) == 0x12) return;
    if (cpsr & (1 << 7)) {
        return;
    }
    dbg << "[IRQ_ENTER_BEGIN] PC=0x" << std::hex << reg[15]
        << " CPSR=0x" << cpsr
        << " T=" << ((cpsr >> 5) & 1)
        << " MODE=0x" << (cpsr & 0x1F)
        << "\n";
    dbg.flush();
    halted = false;
    uint32_t oldCPSR = cpsr;
    bool thumb = (cpsr >> 5) & 1;
    uint32_t returnAddr = reg[15] + 4;
    dbg << "[IRQ_RETURN_ADDR] thumb=" << thumb
        << " reg15=0x" << std::hex << reg[15]
        << " returnAddr=0x" << returnAddr << "\n";
    dbg.flush();
    if (returnAddr >= 0x081E3560 && returnAddr <= 0x081E3580) {
        dbg << "[IRQ_RETURN_NEAR_TM3LOOP] returnAddr=0x" << std::hex << returnAddr
            << " interruptedPC=0x" << reg[15]
            << "\n";
        dbg.flush();
    }
    spsr_irq = oldCPSR;
    switchMode(MODE_IRQ);
    dbg << "[IRQ_SP] SP_irq=0x" << std::hex << reg[13] << "\n";
    dbg.flush();
    cpsr = (oldCPSR & ~0x3F) | MODE_IRQ | (1 << 7); // I=1, T=0
    reg[14] = returnAddr;
    reg[15] = 0x18;

    uint32_t handlerAddr = bus.read32(0x3007FFC);
    dbg << "[ISR_ENTRY] handler=0x" << std::hex << handlerAddr
        << " PC=0x" << reg[15] << "\n";

    dbg << "[IRQ_ENTER_END] PC=0x" << std::hex << reg[15]
        << " LR_irq=0x" << reg[14]
        << " SPSR_irq=0x" << spsr_irq
        << " CPSR=0x" << cpsr << "\n";
    dbg.flush();
}

void CPU::triggerFIQ() {
    dbg << "[FIQ_TRIGGER] PC=0x" << std::hex << reg[15]
        << " CPSR=0x" << cpsr << "\n";
    dbg.flush();
    if (cpsr & (1 << 6)) {
        dbg << "[FIQ_MASKED]\n";
        dbg.flush();
        return;
    }
    uint32_t oldCPSR = cpsr;
    spsr_fiq = oldCPSR;
    switchMode(MODE_FIQ);
    cpsr = (oldCPSR & 0xF0000000) | MODE_FIQ | (1 << 7) | (1 << 6);
    cpsr &= ~(1 << 5);
    reg[14] = reg[15] + 4;
    reg[15] = 0x1C;
}

int CPU::Execute(uint32_t instruction) {
    uint8_t condition = (instruction >> 28);
    if (!checkCondition(condition)) return 1;

    if ((instruction & 0x0FFFFFF0) == 0x012FFF10) {
        executeBx(instruction);
        return 3;
    }

    uint8_t type = (instruction >> 25) & 0x7;

    switch (type) {
    case 0b101:
        executeBranch(instruction);
        return 3;

    case 0b000:
    case 0b001: {
        bool bit25 = (instruction >> 25) & 1;
        bool bit4 = (instruction >> 4) & 1;
        bool bit7 = (instruction >> 7) & 1;

        if ((instruction & 0x0FB00FF0) == 0x01000090) {
            executeSWP(instruction);
            return 3;
        }
        else if ((instruction & 0x0FBF0FFF) == 0x010F0000) {
            executePSRTransfer(instruction);
            return 1;
        }
        else if (!bit25 && !bit4 && (instruction & 0x0DB0F000) == 0x0120F000) {
            executePSRTransfer(instruction);
            return 1;
        }
        else if (bit25 && (instruction & 0x0FB0F000) == 0x0320F000) {
            executePSRTransfer(instruction);
            return 1;
        }
        else if (!bit25 && ((instruction >> 22) & 0x3F) == 0
            && (instruction & 0xF0) == 0x90) {
            executeMultiply(instruction);
            return 2;
        }
        else if (!bit25 && ((instruction >> 23) & 0x1F) == 1
            && (instruction & 0xF0) == 0x90) {
            executeMultiplyLong(instruction);
            return 3;
        }
        else if (!bit25 && bit7 && bit4 && ((instruction >> 5) & 0x3) != 0) {
            executeHalfwordTransfer(instruction);
            return 2;
        }
        else {
            executeDataProcessing(instruction);
            return 1;
        }
    }

    case 0b010:
    case 0b011:
        executeLoadStore(instruction);
        return 3;

    case 0b100:
        executeLoadStoreMultiple(instruction);
        return 4;

    case 0b110:
        return 1;

    case 0b111:
        executeSWI(instruction);
        return 3;
    }

    return 1;
}
//THUMB MODE 

void CPU::thumbMoveShifted(uint16_t instruction) {
    uint8_t shift_type = (instruction >> 11) & 0x3;
    uint8_t offset = (instruction >> 6) & 0x1F;
    uint8_t Rs = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;
   /* dbg << "MoveShifted: instr=0x" << std::hex << instruction
        << " shift_type=" << (int)shift_type
        << " offset=" << std::dec << (int)offset
        << " Rs=R" << (int)Rs << "=0x" << std::hex << reg[Rs]
        << " Rd=R" << (int)Rd << "\n";
    dbg.flush();*/
    uint32_t val = reg[Rs];
    uint32_t result = val;

    switch (shift_type) {
    case 0b00: // LSL
        if (offset == 0) {
            // LSL #0: result and carry unchanged
        }
        else {
            cpsr = (val >> (32 - offset)) & 1 ? (cpsr | (1u << 29)) : (cpsr & ~(1u << 29));
            result = val << offset;
        }

        break;
    case 0b01: // LSR
        if (offset == 0) { // LSR #0 is actually LSR #32
            cpsr = (val >> 31) & 1 ? (cpsr | (1u << 29)) : (cpsr & ~(1u << 29));
            result = 0;
        }
        else {
            cpsr = (val >> (offset - 1)) & 1 ? (cpsr | (1u << 29)) : (cpsr & ~(1u << 29));
            result = val >> offset;
        }
        break;
    case 0b10: // ASR
        if (offset == 0) { // ASR #0 is actually ASR #32
            cpsr = (val >> 31) & 1 ? (cpsr | (1u << 29)) : (cpsr & ~(1u << 29));
            result = (val & (1u << 31)) ? 0xFFFFFFFF : 0;
        }
        else {
            cpsr = (val >> (offset - 1)) & 1 ? (cpsr | (1u << 29)) : (cpsr & ~(1u << 29));
            result = (uint32_t)((int32_t)val >> offset);
        }
        break;
    }
    /*std::cout << "IMM LSL: r" << (int)Rd << " = 0x" << std::hex << result
        << " offset=" << (int)offset << "\n";*/
    reg[Rd] = result;
    (result >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); // N
    (result == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));  // Z
}

void CPU::thumbAddSubtract(uint16_t instruction) {
    uint8_t Rd = instruction & 0x7;
    uint8_t Rs = (instruction >> 3) & 0x7;
    uint8_t I = (instruction >> 10) & 0x1;
    bool op= (instruction >> 9) & 0x1;

    uint32_t value  ;
    if (I)
        value = (instruction >> 6) & 0x7;
    else
    {
        uint8_t Rn = (instruction >> 6) & 0x7;
        value = reg[Rn];
    }
    uint64_t result;
    if (op)
        result = (uint64_t)reg[Rs]-value;
    else
        result = (uint64_t)reg[Rs] + value;

    reg[Rd] = (uint32_t)result;

    setFlags(reg[Rd], result, reg[Rs],value, op);
    
}

void CPU::thumbMoveImmediate(uint16_t instruction) {
    uint8_t op = (instruction >> 11) & 0x3;
    uint8_t Rd = (instruction >> 8) & 0x7;
    uint8_t offset = instruction & 0xFF;  // unsigned, 0-255
    switch (op) {
    case 0b00: // MOV — only N, Z
        reg[Rd] = offset;
        if (Rd == 0 && offset == 0x83) {
            dbg << "[MOV_R0_83] realPC=0x" << std::hex << reg[15]
                << " R0_after_mov=0x" << reg[0] << "\n";
            dbg.flush();
        }
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); // Z
        cpsr &= ~(1u << 31); // N always 0, offset fits in 8 bits
        break;
    case 0b01: { // CMP — doesn't write Rd
        uint32_t original = reg[Rd];
        uint64_t full = (uint64_t)original - offset;
        setFlags((uint32_t)full, full, original, (uint32_t)offset, true);
        break;
    }
    case 0b10: { // ADD
        uint32_t original = reg[Rd];
        uint64_t full = (uint64_t)original + offset;
        reg[Rd] = (uint32_t)full;
        setFlags(reg[Rd], full, original, (uint32_t)offset, false);
        break;
    }
    case 0b11: { // SUB
        uint32_t original = reg[Rd];
        uint64_t full = (uint64_t)original - offset;
        reg[Rd] = (uint32_t)full;
        setFlags(reg[Rd], full, original, (uint32_t)offset, true);
        break;
    }
    }
}
void CPU::thumbDataProcessing(uint16_t instruction) {
    uint8_t opcode = (instruction >> 6) & 0xF;
    uint8_t Rd = instruction & 0x7;
    uint8_t Rs = (instruction >> 3) & 0x7;

    switch (opcode) {
    case 0b0000://AND
        reg[Rd] &= reg[Rs];
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31));
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));
        break;

    case 0b0001://EOR
        reg[Rd] ^= reg[Rs];
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31));
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));
        break;

    case 0b0010: { // LSL (register)
       
        uint8_t amount = reg[Rs] & 0xFF;
        if (amount == 0 ) {
            // Do nothing, Carry remains unchanged
        }
        else if (amount < 32) {
            cpsr = (reg[Rd] >> (32 - amount)) & 1 ? (cpsr | (1u << 29)) : (cpsr & ~(1u << 29));
            reg[Rd] <<= amount;
        }
        else if (amount == 32) {
            uint32_t oldVal = reg[Rd];               // save before zeroing
            if (oldVal & 1) {
                (cpsr |= (1u << 29));
            }
            else
                (cpsr &= ~(1u << 29));
            
            reg[Rd] = 0;
        }
        else {

            cpsr &= ~(1u << 29); // Carry is 0
            reg[Rd] = 0;
        }
        //std::cout << " \n LSL result: r" << (int)Rd << " = 0x" << std::hex << reg[Rd] << "\n";
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); // N
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); // Z
        break;
    }

    case 0b0011: { // LSR (register)
        uint8_t amount = reg[Rs] & 0xFF;
        if (amount == 0) {
            // Do nothing
        }
        else if (amount < 32) {
            cpsr = (reg[Rd] >> (amount - 1)) & 1 ? (cpsr | (1u << 29)) : (cpsr & ~(1u << 29));
            reg[Rd] >>= amount;
        }
        else if (amount == 32) {
            cpsr = (reg[Rd] >> 31) & 1 ? (cpsr | (1u << 29)) : (cpsr & ~(1u << 29));
            reg[Rd] = 0;
        }
        else {
            cpsr &= ~(1u << 29);
            reg[Rd] = 0;
        }
        // Update N, Z flags
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); // N
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); // Z
        break;
    }

    case 0b0100: {  //ASR
        uint8_t amount = reg[Rs] & 0xFF;
        if (amount > 0) {
            if (amount < 32)
                (reg[Rd] >> (amount - 1)) & 1
                ? (cpsr |= (1u << 29))
                : (cpsr &= ~(1u << 29));  // C = last bit shifted out
            else
                (reg[Rd]>>31)& 1 ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
           // std::cout << "ASR: amount=" << (int)amount << " Rd=0x" << std::hex << reg[Rd] << "\n";

            reg[Rd] = (amount >= 32)
                ? (uint32_t)((int32_t)reg[Rd] >> 31)    
                : (uint32_t)((int32_t)reg[Rd] >> amount); 
           // std::cout << "AFTER WRITE: reg[" << (int)Rd << "] = 0x" << std::hex << reg[Rd] << "\n";
        }

        // N and Z always updated
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31));
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));
        //std::cout << " \n ASR result: r" << (int)Rd << " = 0x" << std::hex << reg[Rd] << "\n";
        break;
    }
    case 0b0101: { //ADC
        uint32_t oldRd = reg[Rd];
        uint8_t carry = (cpsr >> 29) & 1;
        uint64_t full  = (uint64_t)reg[Rd] + reg[Rs] + carry;
        reg[Rd] = (uint32_t)full;
        setFlags(reg[Rd], full, oldRd, reg[Rs], false);
        break;
    }
    case 0b0110: { // SBC
        uint32_t val1 = reg[Rd];
        uint32_t val2 = reg[Rs];
        uint32_t carry = (cpsr >> 29) & 1;

        // SBC: Result = Op1 - Op2 - (1 - Carry)
        uint64_t full = (uint64_t)val1 - (uint64_t)val2 - (carry ? 0 : 1);
        reg[Rd] = (uint32_t)full;

        // For SBC, C is 1 if NO borrow occurred (Op1 >= Op2 + !Carry)
        setFlags(reg[Rd], full, val1, val2, true);
        break;
    }
    case 0b0111: {// ROR
        uint8_t amount = reg[Rs] & 0xFF;
        if (amount > 0) {
            (reg[Rd] >> (amount - 1)) & 1 ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));
            reg[Rd] = rotate_right((uint32_t)reg[Rd], amount & 0x1F); //keeping rotating range between 1 and 31

        }
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); //N
        (reg[Rd] ==0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); //Z
        break;
    }
    case 0b1000: { //TST
        uint32_t result = reg[Rd] & reg[Rs];
        (result >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); //N
        (result == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); //Z
        break;
    }
    case 0b1001: { //NEG

        uint64_t full= (uint64_t)0 - reg[Rs];
        reg[Rd] = (uint32_t)full;
        setFlags(reg[Rd], full,0, reg[Rs], true);
        break;
    }
    case 0b1010: { //CMP
        uint64_t full = (uint64_t)reg[Rd] - reg[Rs];
        uint32_t result = (uint32_t)full;
        setFlags(result, full, reg[Rd], reg[Rs], true);
        break;
    }

    case 0b1011: { //CMN
        uint64_t full = (uint64_t)reg[Rd] + reg[Rs];
        uint32_t result = (uint32_t)full;
        setFlags(result, full, reg[Rd], reg[Rs], false);
        break;
    }
    case 0b1100: {//OR
        reg[Rd] |= reg[Rs];
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); //N
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); //Z
        break;
    }
    case 0b1101: {//MUL
        reg[Rd] *= reg[Rs];
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); //N
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); //Z
        break;
    }
    case 0b1110: { //BIC
        reg[Rd] = reg[Rd] & ~reg[Rs];
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); //N
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); //Z
        break;
    }
    case 0b1111: {//MVN
        reg[Rd] = ~(reg[Rs]);
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31)); //N
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30)); //Z
        break;
    }
    }
    


}

void CPU::thumbHiRegister(uint16_t instruction) {
    uint8_t rd_low = instruction & 0x7;
    uint8_t MSBd = (instruction >> 7) & 0x1;
    uint8_t Rd = rd_low | (MSBd << 3);
    uint8_t Rs = (instruction >> 3) & 0xF;
    uint8_t opcode = (instruction >> 8) & 0x3;

    uint32_t pipeline_pc = reg[15] + 2;

    switch (opcode) {
    case 0b00: { // ADD
        uint32_t op1 = (Rd == 15) ? pipeline_pc : reg[Rd];
        uint32_t op2 = (Rs == 15) ? pipeline_pc : reg[Rs];
        uint32_t result = op1 + op2;
        if (Rd == 15) {
            reg[15] = result & ~1u;
        }
        else {
            reg[Rd] = result;
        }
        break;
    }
    case 0b01: { // CMP
        uint32_t op1 = (Rd == 15) ? pipeline_pc : reg[Rd];
        uint32_t op2 = (Rs == 15) ? pipeline_pc : reg[Rs];
        uint64_t full = (uint64_t)op1 - op2;
        setFlags((uint32_t)full, full, op1, op2, true);
        break;
    }
    case 0b10: { // MOV
        uint32_t op2 = (Rs == 15) ? pipeline_pc : reg[Rs];
        reg[Rd] = op2;
        if (Rd == 15) {
            reg[15] &= ~1u;
        }
        break;
    }
    case 0b11: { // BX
        uint32_t op2 = (Rs == 15) ? pipeline_pc : reg[Rs];
        if (op2 & 1) {
            cpsr |= (1u << 5);
            reg[15] = op2 & ~1u;
        }
        else {
            cpsr &= ~(1u << 5);
            reg[15] = op2 & ~3u;
        }
        break;
    }
    }
}

void CPU::thumbPCRelativeLDR(uint16_t instruction) {
    uint8_t Rd = (instruction >> 8) & 0x7;
    uint32_t word8 = instruction & 0xFF;

    uint32_t pipeline_pc = reg[15] + 2;
    uint32_t address = (pipeline_pc & ~2u) + (word8 * 4);

    uint32_t value = bus.read32(address & ~3);
    uint8_t rotation = (address & 3) * 8;
    reg[Rd] = rotate_right(value, rotation);
}

void CPU::thumbLoadStoreRegister(uint16_t instruction) {
    uint8_t opcode = (instruction >> 10) & 0x3;
    uint8_t Rb = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;
    uint8_t R0 = (instruction >> 6) & 0x7;
    switch (opcode) {
    case 0b00: { // STR
        bus.write32((reg[Rb] + reg[R0]), reg[Rd]);
        break;
    }
    case 0b01: { // STRB
        bus.write8((reg[Rb] + reg[R0]), (reg[Rd] & 0xFF));
        break;
    }
    case 0b10: { // LDR
        uint32_t addr = reg[Rb] + reg[R0];
        uint32_t value = bus.read32(addr & ~3);
        uint8_t rotation = (addr & 3) * 8;
        reg[Rd] = rotate_right(value, rotation);
        break;
    }
    case 0b11: { // LDRB
        reg[Rd] = bus.read8((reg[Rb] + reg[R0]));
        break;
    }
    }
}

void CPU::thumbLoadStoreSign(uint16_t instruction) {
    uint8_t opcode = (instruction >> 10) & 0x3;
    uint8_t Rb = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;
    uint8_t R0 = (instruction >> 6) & 0x7;
    uint32_t address = reg[Rb] + reg[R0];

    switch (opcode) {
    case 0b00: { // STRH
        uint32_t storeAddr = address & ~1u;
        if (storeAddr == 0x0400010C || storeAddr == 0x0400010E) {
            dbg << "[STRH_TM3] (thumbLoadStoreSign) addr=0x" << std::hex << storeAddr
                << " val=0x" << (reg[Rd] & 0xFFFF)
                << " realPC=0x" << reg[15]
                << " Rb=" << std::dec << (int)Rb << "=0x" << std::hex << reg[Rb]
                << " R0=" << std::dec << (int)R0 << "=0x" << std::hex << reg[R0]
                << "\n";
            dbg.flush();
        }
        bus.write16(storeAddr, reg[Rd] & 0xFFFF);
        break;
    }
    case 0b01: { // LDRSB
        uint32_t value = bus.read8(address);
        if (value & 0x80) value |= 0xFFFFFF00;
        reg[Rd] = value;
        break;
    }
    case 0b10: { // LDRH
        uint32_t val = bus.read16(address & ~1u);
        uint8_t rotation = (address & 1) * 8;
        reg[Rd] = rotate_right(val, rotation);
        break;
    }
    case 0b11: { // LDRSH
        if (address & 1) {
            uint32_t value = bus.read8(address);
            if (value & 0x80) value |= 0xFFFFFF00;
            reg[Rd] = value;
        }
        else {
            uint32_t value = bus.read16(address);
            if (value & 0x8000) value |= 0xFFFF0000;
            reg[Rd] = value;
        }
        break;
    }
    }
}

void CPU::thumbLoadStoreImmediate(uint16_t instruction) {
    uint8_t opcode = (instruction >> 11) & 0x3;
    uint8_t offset = (instruction >> 6) & 0x1F;
    uint8_t Rb = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;

    uint32_t address;

    switch (opcode) {
    case 0b00: // STR — word
        address = reg[Rb] + (offset * 4);
        bus.write32(address, reg[Rd]);
        break;
    case 0b01: { // LDR — word
        address = reg[Rb] + (offset * 4);
        uint32_t value = bus.read32(address & ~3);
        uint8_t rotation = (address & 3) * 8;
        reg[Rd] = rotate_right(value, rotation);
        break;
    }
    case 0b10: // STRB — byte
        address = reg[Rb] + offset;
        bus.write8(address, reg[Rd] & 0xFF);
        break;
    case 0b11: // LDRB — byte
        address = reg[Rb] + offset;
        reg[Rd] = bus.read8(address);
        break;
    }
}

void CPU::thumbLoadStoreHalfword(uint16_t instruction) {
    uint8_t opcode = (instruction >> 11) & 1;
    uint8_t nn = (instruction >> 6) & 0x1F;
    uint8_t Rb = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;
    uint32_t address;
    switch (opcode) {
    case 0: { // STRH
        address = reg[Rb] + (nn * 2);
        uint32_t storeAddr = address & ~1u;
        if (storeAddr == 0x0400010C || storeAddr == 0x0400010E) {
            dbg << "[STRH_TM3] (thumbLoadStoreHalfword) addr=0x" << std::hex << storeAddr
                << " val=0x" << (reg[Rd] & 0xFFFF)
                << " realPC=0x" << reg[15]
                << " Rb=" << std::dec << (int)Rb << "=0x" << std::hex << reg[Rb]
                << " nn=" << std::dec << (int)nn
                << "\n";
            dbg.flush();
        }
        bus.write16(storeAddr, reg[Rd] & 0xFFFF);
        break;
    }
    case 1: { // LDRH
        address = reg[Rb] + (nn * 2);
        uint32_t val = bus.read16(address & ~1u);
        uint8_t rotation = (address & 1) * 8;
        reg[Rd] = rotate_right(val, rotation);
        break;
    }
    }
}

void CPU::thumbSPRelative(uint16_t instruction) {
    uint8_t opcode = (instruction >> 11) & 1;
    uint8_t nn = instruction & 0xFF;
    uint8_t Rd = (instruction >> 8) & 0x7;
    uint32_t SP = reg[13];
    uint32_t address = SP + (nn * 4);
    switch (opcode) {
    case 0: { // STR
        bus.write32(address, reg[Rd]);
        break;
    }
    case 1: { // LDR
        uint32_t value = bus.read32(address & ~3);
        uint8_t rotation = (address & 3) * 8;
        reg[Rd] = rotate_right(value, rotation);
        break;
    }
    }
}

void CPU::thumbLoadAddress(uint16_t instruction) {
    uint8_t opcode = (instruction >> 11) & 1;
    uint8_t nn = instruction & 0xFF;
    uint8_t Rd = (instruction >> 8) & 0x7;

    switch (opcode) {
    case 0: { // ADD Rd, PC, #nn
        // Architecture requires: (PC + 4) & ~2
        uint32_t pipeline_pc = reg[15] + 2;
        reg[Rd] = (pipeline_pc & ~2u) + (nn * 4);
        break;
    }
    case 1: { // ADD Rd, SP, #nn
        reg[Rd] = reg[13] + (nn * 4);
        break;
    }
    }
}

void CPU::thumbAddSP(uint16_t instruction) {
   
    uint8_t nn = instruction & 0x7F;
    uint8_t opcode = (instruction >> 7) & 1;
    switch (opcode) {
    case 0: { //ADD NN
        reg[13] += (nn * 4);
        break;
    }
    case 1: {// ADD -NN
        reg[13] -= (nn * 4);
        break;
    }
    }
}

void CPU::thumbPushPop(uint16_t instruction) {
    bool L = (instruction >> 11) & 1;
    bool R = (instruction >> 8) & 1;
    uint8_t regList = instruction & 0xFF;
    uint32_t address = reg[13];

    if (!L) { // PUSH
        uint8_t count = 0;
        for (int i = 0; i < 8; i++) if (regList & (1 << i)) count++;
        if (R) count++;
        address -= (count * 4);
        reg[13] = address;
        for (int i = 0; i < 8; i++) {
            if (regList & (1 << i)) {
                bus.write32(address, reg[i]);
                address += 4;
            }
        }
        if (R) bus.write32(address, reg[14]);
    }
    else { // POP
        for (int i = 0; i < 8; i++) {
            if (regList & (1 << i)) {
                reg[i] = bus.read32(address);
                address += 4;
            }
        }
        if (R) {
            uint32_t target = bus.read32(address);
            address += 4;
            reg[13] = address;
            reg[15] = target & ~1u;
            return;
        }
        reg[13] = address;
    }
}
void CPU::thumbMultipleLoadStore(uint16_t instruction) {
    uint8_t L = (instruction >> 11) & 1;
    uint8_t Rb = (instruction >> 8) & 0x7;
    uint8_t regList = instruction & 0xFF;
    uint32_t address = reg[Rb];

    if (regList == 0) {
        if (L) { // LDMIA Rb!, {}
            reg[15] = bus.read32(address) & ~1u;
        }
        else {   // STMIA Rb!, {}
            // Write PC + 6. Since reg[15] is currently PC + 2, we add 4.
            uint32_t pipeline_pc = reg[15] + 4;
            bus.write32(address, pipeline_pc);
        }
        // Both write back a offset of 0x40 (equivalent to 16 registers)
        reg[Rb] += 0x40;
        return;
    }
    uint32_t count = 0;
    for (int i = 0; i <= 7; i++)
        if (regList & (1u << i)) count++;

    uint32_t writebackAddr = address + (count * 4);
    bool rbInList = (regList >> Rb) & 1;

    // Find lowest register in list
    int lowestReg = -1;
    for (int i = 0; i <= 7; i++)
        if (regList & (1u << i)) { lowestReg = i; break; }

    for (int i = 0; i <= 7; i++) {
        if (regList & (1u << i)) {
            if (L) {
                reg[i] = bus.read32(address);
            }
            else {
                // Rb not lowest → store writeback value
                uint32_t val = (i == Rb && i != lowestReg)
                    ? writebackAddr : reg[i];
                bus.write32(address, val);
            }
            address += 4;
        }
    }

    if (!L || !rbInList)
        reg[Rb] = writebackAddr;
}

void CPU::thumbConditionalBranch(uint16_t instruction) {
    uint8_t cond = (instruction >> 8) & 0xF;
    if (cond == 0xF) {
        uint8_t swiNum = instruction & 0xFF;
        dbg << "[SWI] num=0x" << std::hex << (int)swiNum
            << " PC=0x" << reg[15]
            << " R0=0x" << reg[0] << "\n";
        dbg.flush();
        executeSWI((uint32_t)instruction);
        return;
    }
    if (checkCondition(cond)) {
        int32_t offset = (int8_t)(instruction & 0xFF);
        reg[15] = (reg[15] + 2) + (offset << 1);
    }
}

void CPU::thumbUnconditionalBranch(uint16_t instruction) {
    int32_t offset = instruction & 0x7FF;

    // sign extend 11-bit
    if (offset & 0x400)
        offset |= ~0x7FF;

    uint32_t pc = reg[15] + 2; // pipelined PC
    reg[15] = pc + (offset << 1);
}
void CPU::thumbLongBranch(uint16_t instruction) {
    uint8_t H = (instruction >> 11) & 1;
    int32_t imm11 = (int32_t)(int16_t)((instruction & 0x7FF) << 5) >> 5;

    if (H == 0) {
        // First half: LR = PC + (imm11 << 12)
        uint32_t pc = reg[15] + 2;  // Thumb pipeline: instr_addr + 4
        reg[14] = pc + (imm11 << 12);
    }
    else {
        // Second half: compute target from FIRST-HALF LR before overwriting it
        uint32_t target = reg[14] + (imm11 << 1);  // use LR set by first half
        reg[14] = reg[15] | 1;                      // LR = return addr | Thumb bit
        reg[15] = target & ~1;                      // jump
    }
}

int CPU::executeThumb(uint16_t instruction) {
    uint8_t top5 = instruction >> 11;
    uint8_t top6 = instruction >> 10;
    uint8_t top8 = instruction >> 8;

    if ((top5 & 0b11111) == 0b00011) {
        thumbAddSubtract(instruction);
        return 1;
    }
    else if ((top5 & 0b11100) == 0b00000) {
        thumbMoveShifted(instruction);
        return 1;
    }
    else if ((top5 & 0b11100) == 0b00100) {
        thumbMoveImmediate(instruction);
        return 1;
    }
    else if (top6 == 0b010000) {
        thumbDataProcessing(instruction);
        return 1;
    }
    else if (top6 == 0b010001) {
        thumbHiRegister(instruction);
        return 3;
    }
    else if ((top5 & 0b11111) == 0b01001) {
        thumbPCRelativeLDR(instruction);
        return 2;
    }
    else if ((top5 & 0b11110) == 0b01010) {
        if ((instruction >> 9) & 1) {
            thumbLoadStoreSign(instruction);
            return 2;
        }
        else {
            thumbLoadStoreRegister(instruction);
            return 2;
        }
    }
    else if ((top5 & 0b11100) == 0b01100) {
        thumbLoadStoreImmediate(instruction);
        return 2;
    }
    else if ((top5 & 0b11110) == 0b10000) {
        thumbLoadStoreHalfword(instruction);
        return 2;
    }
    else if ((top5 & 0b11110) == 0b10010) {
        thumbSPRelative(instruction);
        return 2;
    }
    else if ((top5 & 0b11110) == 0b10100) {
        thumbLoadAddress(instruction);
        return 1;
    }
    else if (top8 == 0b10110000) {
        thumbAddSP(instruction);
        return 1;
    }
    else if ((top8 & 0b11110110) == 0b10110100) {
        thumbPushPop(instruction);
        return 3;
    }
    else if ((top5 & 0b11110) == 0b11000) {
        thumbMultipleLoadStore(instruction);
        return 4;
    }
    // --- FIXED SWI DECODING INTERCEPT ---
    else if (top8 == 0b11011111) { // 0xDF - Software Interrupt (SWI / SVC)
        executeSWI(instruction);
        return 3;
    }
    // ------------------------------------
    else if ((top5 & 0b11110) == 0b11010) {
        thumbConditionalBranch(instruction);
        return 3;
    }
    else if ((top5 & 0b11111) == 0b11100) {
        thumbUnconditionalBranch(instruction);
        return 3;
    }
    else if (top5 == 0b11110) {
        int32_t nn = instruction & 0x7FF;
        if (nn & (1 << 10)) nn |= 0xFFFFF800;
        reg[14] = reg[15] + 2 + (nn << 12);
        return 1;
    }
    else if (top5 == 0b11111) {
        uint32_t nn = instruction & 0x7FF;
        uint32_t nextInstr = reg[15] | 1;
        reg[15] = reg[14] + (nn << 1);
        reg[14] = nextInstr;
        return 3;
    }
    else if (top5 == 0b11101) {
        return 1;
    }

    return 1;
}
