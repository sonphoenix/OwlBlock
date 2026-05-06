#include"cpu.h"
#include"gba_registers.h"
#include <iomanip>

extern std::ofstream dbg;

void CPU::switchMode(uint8_t newMode, bool returning) {
    uint8_t oldMode = cpsr & 0x1F;

    // --- STEP 1: Bank OUT (Save current regs to the OLD mode's bank) ---
    if (oldMode == MODE_FIQ) {
        for (int i = 0; i < 5; i++) reg_fiq[i] = reg[i + 8];
        reg_fiq[5] = reg[13]; reg_fiq[6] = reg[14];
    }
    else if (oldMode == MODE_IRQ) {
        reg_irq[0] = reg[13]; reg_irq[1] = reg[14];
    }
    else if (oldMode == MODE_SUPERVISOR) {
        reg_svc[0] = reg[13]; reg_svc[1] = reg[14];
    }
    else if (oldMode == MODE_ABORT) {
        reg_abt[0] = reg[13]; reg_abt[1] = reg[14];
    }
    else if (oldMode == MODE_UNDEFINED) {
        reg_und[0] = reg[13]; reg_und[1] = reg[14];
    }
    else { // User or System
        for (int i = 0; i < 5; i++) reg_user[i] = reg[i + 8];
        reg_user[5] = reg[13]; reg_user[6] = reg[14];
    }

    // --- STEP 2: Update CPSR / SPSR ---
    if (returning) {
        // Restore the full CPSR from the SPSR of the mode we are LEAVING
        switch (oldMode) {
        case MODE_FIQ:        cpsr = spsr_fiq; break;
        case MODE_IRQ:        cpsr = spsr_irq; break;
        case MODE_SUPERVISOR: cpsr = spsr_svc; break;
        case MODE_ABORT:      cpsr = spsr_abt; break;
        case MODE_UNDEFINED:  cpsr = spsr_und; break;
        default: break; // User/System have no SPSR to return from
        }
        // The mode we are switching TO is now defined by the restored CPSR
        newMode = cpsr & 0x1F;
    }
    else {
        // Normal mode switch (e.g., MSR or Exception Entry)
        // If entering a privileged mode, save current CPSR into the new SPSR
        switch (newMode) {
        case MODE_FIQ:        spsr_fiq = cpsr; break;
        case MODE_IRQ:        spsr_irq = cpsr; break;
        case MODE_SUPERVISOR: spsr_svc = cpsr; break;
        case MODE_ABORT:      spsr_abt = cpsr; break;
        case MODE_UNDEFINED:  spsr_und = cpsr; break;
        }
        cpsr = (cpsr & ~0x1F) | newMode;
    }

    // --- STEP 3: Bank IN (Load regs from the NEW mode's bank into active 'reg') ---
    if (newMode == MODE_FIQ) {
        for (int i = 0; i < 5; i++) reg[i + 8] = reg_fiq[i];
        reg[13] = reg_fiq[5]; reg[14] = reg_fiq[6];
    }
    else {
        // All non-FIQ modes use User/System R8-R12
        for (int i = 0; i < 5; i++) reg[i + 8] = reg_user[i];

        if (newMode == MODE_IRQ) {
            reg[13] = reg_irq[0]; reg[14] = reg_irq[1];
        }
        else if (newMode == MODE_SUPERVISOR) {
            reg[13] = reg_svc[0]; reg[14] = reg_svc[1];
        }
        else if (newMode == MODE_ABORT) {
            reg[13] = reg_abt[0]; reg[14] = reg_abt[1];
        }
        else if (newMode == MODE_UNDEFINED) {
            reg[13] = reg_und[0]; reg[14] = reg_und[1];
        }
        else { // User or System
            reg[13] = reg_user[5]; reg[14] = reg_user[6];
        }
    }
}

void CPU::skipBIOS() {
    //printf("\nSKIP GBA BIOS - Jumping directly to ROM\n");

    // Clear WRAM (0x3007E00 to 0x3008000) - stacks and BIOS IRQ vector/flags
    for (uint32_t i = 0x3007E00; i < 0x3008000; i++) {
        bus.write8(i, 0);
    }

    // Clear R0-R12
    for (int i = 0; i < 13; i++) {
        reg[i] = 0;
    }

    // Clear banked registers
    reg_svc[1] = 0;   // R14_svc
    reg_irq[1] = 0;   // R14_irq
    spsr_svc = 0;
    spsr_irq = 0;

    // Set CPSR to System mode
    cpsr = 0x0000001F;  // System mode, ARM, IRQ/FIQ enabled

    // Set stack pointers
    reg_svc[0] = 0x03007FE0;   // SP_svc
    reg_irq[0] = 0x03007FA0;   // SP_irq
    reg[13] = 0x03007F00;      // SP (User/System)

    // Jump to ROM entry point
    reg[15] = MEM_ROM;

    // Ensure ARM mode (clear Thumb bit)
    cpsr &= ~(1 << 5);
}

void CPU::Step() {

    if (halted) {
        //do nothing
        return;
    }
    uint32_t fetch_pc = reg[15]; // In ARM, reg[15] is usually the instruction being executed + 8

    if (cpsr & (1 << 5)) { // THUMB
        uint16_t instr = bus.read16(fetch_pc);

        // BIOS Protection: Even in Thumb mode, the latch is updated 
        // with the 32-bit word aligned at that address.
        if (fetch_pc < 0x4000) {
            bus.updateBiosLatch(bus.read32(fetch_pc & ~3));
        }

        reg[15] += 2;
        executeThumb(instr);
    }
    else { // ARM
        uint32_t instr = bus.read32(fetch_pc);

        if (fetch_pc < 0x4000) {
            bus.updateBiosLatch(instr);
        }

        reg[15] += 4;
        Execute(instr);
    }
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
    default:     return true;
    }
}

void CPU::executeBranch(uint32_t instruction) {
    //std::cout << "execute branch called \n";
    bool link = (instruction >> 24) & 1;
    int32_t offset = instruction & 0xFFFFFF;

    if (offset & 0x800000) offset |= 0xFF000000; // Sign extend

    if (link) {
        // Return address is the instruction right after the branch.
        // If reg[15] is currently (InstrAddr + 4), then reg[15] is the return addr.
        reg[14] = reg[15];
    }

    // Target = (Current Instruction Address + 8) + (offset * 4)
    reg[15] = getReg(15) + (offset << 2);
}


void CPU::executeBx(uint32_t instruction) {
    uint8_t Rn = instruction & 0xF;
    uint32_t target = getReg(Rn); // Use getReg!

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
            case 0b00: // LSL #0 — no-op, carry unchanged
                return value;
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
        amount = reg[Rs] & 0xFF;
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
            case 0b11: // ROR — wraps
                amount %= 32;
                if (amount == 0) {
                    // amount was exactly a multiple of 32 — value unchanged, carry = bit 31
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
    uint8_t immediate = (instruction >> 25) & 1;
    uint8_t opcode = (instruction >> 21) & 0xF;
    uint8_t Rd = (instruction >> 12) & 0xF;
    uint8_t S = (instruction >> 20) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;

    bool op2_is_reg_shift = !immediate && ((instruction >> 4) & 1);

    // ADR detection: ADD or SUB with Rn=15, immediate operand
    if (Rn == 15 && ((instruction >> 25) & 1) == 1 && (opcode == 0b0100 || opcode == 0b0010)) {
        uint32_t pc_val = getReg(15);
        uint8_t rotation = (instruction >> 8) & 0xF;
        uint8_t imm8 = instruction & 0xFF;
        uint32_t offset = rotate_right(imm8, rotation * 2);
        uint32_t computed = (opcode == 0b0100) ? (pc_val + offset) : (pc_val - offset);

        dbg << "ADR: PC=" << std::hex << pc_val
            << " offset=0x" << offset
            << " -> r" << std::dec << (int)Rd
            << " = 0x" << std::hex << computed << "\n";
        dbg.flush();
    }

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

    // Only adjust for reg-shift case; getReg(15) already returns instr+8
    uint32_t rnVal;
    if (Rn == 15 && op2_is_reg_shift) {
        rnVal = getReg(15) + 4;  // instr+12
    }
    else {
        rnVal = getReg(Rn);
    }

    // Save carry before getOp2 can clobber it via rotation encoding
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
    // After computing result, before writing to Rd:
    if (Rd == 15) {
        if (S) {
            uint8_t mode = cpsr & 0x1F;
            if (mode != MODE_USER && mode != MODE_SYSTEM) {
                // Exception return: restore CPSR from SPSR
                switchMode(mode, true);  // returning=true
                reg[15] = result & ~3;
                return;
            }
        }
        reg[15] = result & ~3;
        return;
    }

    if (writesResult) {
        reg[Rd] = result;
    }

    if (S) {
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
            int64_t acc = ((int64_t)getReg(RdHi) << 32) | (int32_t)getReg(RdLo);
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

    // step 1 — calculate offset
    uint32_t offset;
    if (I == 0) {
        offset = instruction & 0xFFF;           // plain 12-bit number
    }
    else {
        uint8_t Rm = instruction & 0xF;
        uint8_t shift = (instruction >> 4) & 0xFF;
        offset = apply_shift(getReg(Rm), shift);   // register + shift (fixed)
    }

    // step 2 — calculate the full address (add or subtract offset)
    uint32_t base = getReg(Rn);  
    uint32_t address = U ? (base + offset) : (base - offset);

    // step 3 — pick effective address (pre: use offset address, post: use base)
    uint32_t effective = P ? address : base;

    // step 4 — load or store
    if (L) {
        // LOAD
        if (B) {
            reg[Rd] = bus.read8(effective);                    // LDRB — 1 byte
        }
        else {
            uint32_t value = bus.read32(effective & ~3);       // align to 4 bytes
            uint8_t rotation = (effective & 3) * 8;           // misalignment amount
            reg[Rd] = rotate_right(value, rotation);           // GBA rotation behaviour
        }
    }
    else {
        // STORE
        if (B) {
            bus.write8(effective, reg[Rd] & 0xFF);             // STRB — 1 byte
        }
        else {
            if (Rd == 15) {
                bus.write32(effective & ~3, reg[Rd]+8);
                return;
            }
            bus.write32(effective & ~3, reg[Rd]);                   // STR  — 4 bytes
        }
    }

    // step 5 — writeback
    if (!P || W) {
        if (!L || (Rn != Rd)) { // Don't writeback if LDR overwrote Rn
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

    // Empty list: transfer PC only, base updated as if 16 registers
    if (regList == 0) {
        uint32_t addr = base;
        if (!U) addr -= 64;
        if (P == U) addr += 4;
        if (L) {
            reg[15] = bus.read32(addr & ~3) & ~3;
        }
        else {
            bus.write32(addr & ~3, reg[15] + 8); // PC+12
        }
        if (W) reg[Rn] = U ? (base + 64) : (base - 64);
        return;
    }

    int count = 0;
    for (int i = 0; i < 16; i++) if (regList & (1 << i)) count++;

    // Find lowest register in list (needed for STM base register rule)
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
                if (S && !(regList & (1 << 15))) {
                    // Load into user bank registers
                    if (i >= 8 && i <= 12) reg_user[i - 8] = val;
                    else if (i == 13)            reg_user[5] = val;
                    else if (i == 14)            reg_user[6] = val;
                    else                         reg[i] = val;
                }
                else {
                    reg[i] = val;
                    if (i == 15) reg[15] &= ~3;
                }
            }
            else {
                uint32_t val;
                if (S) {
                    // Store user bank registers
                    val = (i >= 8 && i <= 12) ? reg_user[i - 8] :
                        (i == 13) ? reg_user[5] :
                        (i == 14) ? reg_user[6] : reg[i];
                }
                else if (i == 15) {
                    val = reg[15] + 8; // PC+12
                }
                else if (W && i == Rn && i != lowestReg) {
                    val = writebackVal; // not lowest: store writeback value
                }
                else {
                    val = reg[i]; // lowest or no writeback: store original
                }
                bus.write32(address & ~3, val);
            }
            address += 4;
        }
    }

    if (W && !(L && (regList & (1 << Rn)))) {
        reg[Rn] = writebackVal;
    }
}

void CPU::executePSRTransfer(uint32_t instruction) {
    uint8_t Pd = (instruction >> 22) & 1; // 1=SPSR, 0=CPSR
    uint8_t op = (instruction >> 21) & 1; // 1=MSR, 0=MRS
    uint8_t I = (instruction >> 25) & 1;

    if (op) { // MSR
        uint32_t value;
        if (I) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm = (instruction & 0xFF);
            value = rotate_right(imm, rotation * 2);
        }
        else {
            uint8_t Rm = instruction & 0xF;
            value = getReg(Rm);  // Fixed: use getReg
        }

        uint32_t mask = 0;
        if ((instruction & 0x00080000)) mask |= 0xFF000000; // flags
        if ((instruction & 0x00040000)) mask |= 0x00FF0000; // status
        if ((instruction & 0x00020000)) mask |= 0x0000FF00; // extension
        if ((instruction & 0x00010000)) mask |= 0x000000FF; // control

        uint8_t currentMode = cpsr & 0x1F;
        if (currentMode == MODE_USER) {
            mask &= 0xFF000000;  // User mode can only change flags
        }

        if (Pd) { // SPSR
            switch (currentMode) {
            case MODE_IRQ: spsr_irq = (spsr_irq & ~mask) | (value & mask); break;
            case MODE_FIQ: spsr_fiq = (spsr_fiq & ~mask) | (value & mask); break;
            case MODE_SUPERVISOR: spsr_svc = (spsr_svc & ~mask) | (value & mask); break;
            case MODE_UNDEFINED: spsr_und = (spsr_und & ~mask) | (value & mask); break;
            case MODE_ABORT: spsr_abt = (spsr_abt & ~mask) | (value & mask); break;
            default: break; // User/System can't write SPSR
            }
        }
        else { // CPSR
            uint32_t oldCpsr = cpsr;
            uint32_t newCpsr = (cpsr & ~mask) | (value & mask);

            // Update the non-mode bits immediately
            cpsr = newCpsr;

            uint8_t oldMode = oldCpsr & 0x1F;
            uint8_t newMode = newCpsr & 0x1F;

            if (oldMode != newMode) {
                // Restore the OLD cpsr mode bits temporarily so switchMode 
                // knows which bank we are currently in.
                cpsr = (cpsr & ~0x1F) | oldMode;
                switchMode(newMode, false);
            }
        }
    }
    else { // MRS (Move PSR to Register)
        uint8_t Rd = (instruction >> 12) & 0xF;

        if (Pd) {
            // Read SPSR based on current mode
            uint8_t currentMode = cpsr & 0x1F;
            switch (currentMode) {
            case MODE_IRQ: reg[Rd] = spsr_irq; break;
            case MODE_FIQ: reg[Rd] = spsr_fiq; break;
            case MODE_SUPERVISOR: reg[Rd] = spsr_svc; break;
            case MODE_UNDEFINED: reg[Rd] = spsr_und; break;
            case MODE_ABORT: reg[Rd] = spsr_abt; break;
            default: reg[Rd] = cpsr; break; // User/System mode has no SPSR
            }
        }
        else {
            // Read CPSR
            reg[Rd] = cpsr;
        }

        // Fixed: Safety for PC
        if (Rd == 15) {
            reg[15] &= ~3;
        }
    }
}

void CPU::executeSWI(uint32_t instruction) {
    // If Thumb, reg[15] is already PC+2. If ARM, reg[15] is already PC+4.
    // This is exactly where we want to return.
    uint32_t returnAddress = reg[15];

    uint32_t oldCPSR = cpsr;
    switchMode(MODE_SUPERVISOR, false);

    spsr_svc = oldCPSR; // Ensure SPSR is explicitly saved
    reg[14] = returnAddress;

    cpsr |= (1 << 7);    // Disable IRQ
    cpsr &= ~(1 << 5);   // Force ARM mode for BIOS
    reg[15] = 0x08;      // SWI Vector
}

void CPU::triggerIRQ() {
    halted = false;
    uint32_t oldCPSR = cpsr;

    switchMode(MODE_IRQ, false);


    reg[14] = reg[15];
    spsr_irq = oldCPSR;

    // 4. CRITICAL HARDWARE LOCK:
    cpsr |= (1 << 7);    // Set I-bit to 1 (Disable IRQs)
    cpsr &= ~(1 << 5);   // Clear T-bit (Switch to ARM mode)

    // 5. Jump to IRQ Vector
    reg[15] = 0x18;
}
void CPU::triggerFIQ() {
    if (cpsr & (1 << 6)) return; // Check F-bit (FIQ mask)

    //uint32_t oldCPSR = cpsr;
    switchMode(MODE_FIQ, false);
    //spsr_fiq = oldCPSR;

    reg[14] = reg[15] + 4;
    cpsr |= (1 << 7) | (1 << 6); // FIQ disables BOTH IRQ and FIQ
    cpsr &= ~(1 << 5);           // Force ARM mode
    reg[15] = 0x1C;              // Jump to FIQ Vector
}

void CPU::Execute(uint32_t instruction) {
    uint8_t condition = (instruction >> 28);
    if (!checkCondition(condition)) return;

    if ((instruction & 0x0FFFFFF0) == 0x012FFF10) {
        executeBx(instruction);
        return;
    }

    uint8_t type = (instruction >> 25) & 0x7;

    switch (type) {
    case 0b101:
        executeBranch(instruction);
        break;

    case 0b000:
    case 0b001: {
        bool bit25 = (instruction >> 25) & 1;
        bool bit4 = (instruction >> 4) & 1;
        bool bit7 = (instruction >> 7) & 1;

        // SWP/SWPB
        if ((instruction & 0x0FB00FF0) == 0x01000090) {
            executeSWP(instruction);
        }
        // MRS: bits 11-0 are all 0 (SBZ), so the 0x0FFF mask catches it safely
        else if ((instruction & 0x0FBF0FFF) == 0x010F0000) {
            executePSRTransfer(instruction);
        }
        // MSR register: bit25=0, bit4=0 (bits 11-4 are SBZ in register form)
        else if (!bit25 && !bit4 && (instruction & 0x0DB0F000) == 0x0120F000) {
            executePSRTransfer(instruction);
        }
        // MSR immediate: bit25=1, bits27-24=0011, bits23,21,20=0,1,0, bits15-12=1111
        else if (bit25 && (instruction & 0x0FB0F000) == 0x0320F000) {
            executePSRTransfer(instruction);
        }
        // Multiply: bits27-22=000000, bits7-4=1001
        else if (!bit25 && ((instruction >> 22) & 0x3F) == 0
            && (instruction & 0xF0) == 0x90) {
            executeMultiply(instruction);
        }
        // Multiply Long: bits27-23=00001, bits7-4=1001
        else if (!bit25 && ((instruction >> 23) & 0x1F) == 1
            && (instruction & 0xF0) == 0x90) {
            executeMultiplyLong(instruction);
        }
        // Halfword/Signed transfer: bit25=0, bit7=1, bit4=1, bits6-5 != 00
        else if (!bit25 && bit7 && bit4 && ((instruction >> 5) & 0x3) != 0) {
            executeHalfwordTransfer(instruction);
        }
        else {
            executeDataProcessing(instruction);
        }
        break;
    }

    case 0b010:
    case 0b011:
        executeLoadStore(instruction);
        break;

    case 0b100:
        executeLoadStoreMultiple(instruction);
        break;

    case 0b110:
        // Coprocessor — not used on GBA
        break;

    case 0b111:
        dbg << "swiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii";
        dbg.flush();
        executeSWI(instruction);
        break;
    }
}

//THUMB MODE 

void CPU::thumbMoveShifted(uint16_t instruction) {
    uint8_t shift_type = (instruction >> 11) & 0x3;
    uint8_t offset = (instruction >> 6) & 0x1F;
    uint8_t Rs = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;

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
    switch (opcode) {
    case 0b00:
        if (Rd == 15) {
            reg[15] = (getReg(15) + reg[Rs]) & ~1u;  // use architectural PC, halfword-align result
        }
        else {
            reg[Rd] += reg[Rs];
        }
        break;
    case 0b01: {
        uint64_t full = (uint64_t)reg[Rd] - reg[Rs];
        uint32_t result = (uint32_t)full;
        setFlags(result, full, reg[Rd], reg[Rs], true);
        break;
    }
    case 0b10:
        if (Rs == 15) {
            reg[Rd] = getReg(15) & ~2u;
        }
        else {
            reg[Rd] = reg[Rs];
            if (Rd == 15) {
                reg[15] &= ~1u;
            }
        }
        break;
    case 0b11:
        if (MSBd == 0) { // BX
            if (reg[Rs] & 1) { // bit 0 = 1 → switch to THUMB
                cpsr |= (1u << 5);
                reg[15] = reg[Rs] & ~1u;   // clear flag bit, address is halfword-aligned
            }
            else {              // bit 0 = 0 → stay in ARM
                cpsr &= ~(1u << 5);
                reg[15] = reg[Rs] & ~3u;   // word-align (ARM instructions are 4-byte aligned)
            }
           
        }
        else { // BLX Rn — ARM9 only
        }
        break;
    }
}

void CPU::thumbPCRelativeLDR(uint16_t instruction) {
    uint8_t Rd = (instruction >> 8) & 0x7;
    uint32_t word8 = instruction & 0xFF;

    // reg[15] is currently PC + 2. We need (PC + 4) & ~2.
    // (PC + 2) + 2 = PC + 4.
    uint32_t address = ((reg[15] + 2) & ~2) + (word8 * 4);
    reg[Rd] = bus.read32(address);
}

void  CPU::thumbLoadStoreRegister(uint16_t instruction) {
    uint8_t opcode = (instruction >> 10) & 0x3;
    uint8_t Rb = (instruction >> 3) & 0x7;
    uint8_t Rd = instruction & 0x7;
    uint8_t R0 = (instruction >> 6) & 0x7;
    switch (opcode) {
    case 0b00: {//STR
        bus.write32((reg[Rb] + reg[R0]), reg[Rd]);
        break;
    }
    case 0b01: {//STRB
        bus.write8((reg[Rb] + reg[R0]), (reg[Rd] & 0xFF));
        break;

    }
    case 0b10:{//LSR
        reg[Rd] = bus.read32((reg[Rb] + reg[R0]));
        break;
    }
    case 0b11: {//LDRB
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
    uint32_t address = reg[Rb] + reg[R0];  // raw, no masking here

    switch (opcode) {
    case 0b00: { // STRH
        bus.write16(address & ~1u, reg[Rd] & 0xFFFF);
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
            // misaligned LDRSH acts as LDRSB on ARM7TDMI
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
    case 0b01: // LDR — word
        address = reg[Rb] + (offset * 4);
        reg[Rd] = bus.read32(address);
        break;
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
    case 0: { //STRH
        address = reg[Rb] + (nn * 2);
        bus.write16(address & ~1u, reg[Rd] & 0xFFFF);
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
    //std::cout << "wooooooooooooooooo";
    uint8_t opcode = (instruction>>11) & 1;
    uint8_t nn = instruction  & 0xFF;
    uint8_t Rd=(instruction >> 8) & 0x7;
    uint32_t SP = reg[13];
    uint32_t address = SP + (nn * 4);
    switch (opcode) {
    case 0: {//STR
        bus.write32(address, reg[Rd]);
        break;
    }
    case 1: { //LDR
        reg[Rd] = bus.read32(address);
        break;
    }
    }
}

void CPU:: thumbLoadAddress(uint16_t instruction) {
    uint8_t opcode = (instruction >> 11) & 1;
    uint8_t nn = instruction & 0xFF;
    uint8_t Rd = (instruction >> 8) & 0x7;

    switch (opcode) {
    case 0: { // ADD Rd, PC, #nn
        // Architecture requires (PC + 4) & ~2
       
        uint32_t archPC = getReg(15) & ~3u;
        reg[Rd] = (archPC) + (nn * 4);
       /* std::cout << "ADD PC: archPC=0x" << std::hex << archPC
            << " nn=" << (int)nn
            << " result=0x" << reg[Rd] << "\n";*/
        break;
    }
    case 1: { //ADD SP
        //std::cout << "hellloooo\n";
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
    bool L = (instruction >> 11) & 1; // 1=Pop, 0=Push
    bool R = (instruction >> 8) & 1;  // 1=PC/LR bit
    uint8_t regList = instruction & 0xFF;
    uint32_t address = reg[13];

    if (!L) { // PUSH
        uint8_t count = 0;
        for (int i = 0; i < 8; i++) if (regList & (1 << i)) count++;
        if (R) count++;

        address -= (count * 4);
        reg[13] = address; // Update SP first

        for (int i = 0; i < 8; i++) {
            if (regList & (1 << i)) {
                bus.write32(address, reg[i]);
                address += 4;
            }
        }
        if (R) bus.write32(address, reg[14]); // Push LR
    }
    else { // POP
        for (int i = 0; i < 8; i++) {
            if (regList & (1 << i)) {
                reg[i] = bus.read32(address);
                address += 4;
            }
        }
        if (R) {
            // POP {PC} ignores LSB, remains in Thumb
            reg[15] = bus.read32(address) & ~1u;
            address += 4;
        }
        reg[13] = address;
    }
}

void CPU::thumbMultipleLoadStore(uint16_t instruction) {
    uint8_t L = (instruction >> 11) & 1;
    uint8_t Rb = (instruction >> 8) & 0x7;
    uint8_t regList = instruction & 0xFF;
    uint32_t address = reg[Rb];

    // Empty rlist: transfer PC, base += 0x40
    if (regList == 0) {
        if (L) {
            reg[15] = bus.read32(address) & ~1u;
        }
        else {
            bus.write32(address, getReg(15)+2); 
        }
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
        executeSWI((uint32_t)instruction);
        return;
    }

    if (checkCondition(cond)) {
        // Sign extend 8-bit offset to 32-bit signed int
        int32_t offset = (int8_t)(instruction & 0xFF);

        // Target = (PC + 4) + (offset * 2)
        // reg[15] is PC + 2, so we add 2 more to get PC + 4.
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
    uint8_t H = (instruction >> 11) & 1;   // 0 = first half, 1 = second half
    int32_t imm11 = (int32_t)(int16_t)((instruction & 0x7FF) << 5) >> 5; // sign extend 11 bits

    if (H == 0) {
        // First half: LR = PC + (imm11 << 12)
        // PC = address of this instruction + 4 = (reg[15] - 2) + 4 = reg[15] + 2
        uint32_t pc = reg[15] + 2;
        reg[14] = pc + (imm11 << 12);
    }
    else {
        // Second half: target = LR + (imm11 << 1)
        // Also update LR = address of next instruction (after BL) | 1
        reg[14] = reg[15] | 1;               // return address with Thumb bit set
        reg[15] = reg[14] + (imm11 << 1);    // target = LR + offset*2
    }
}

void CPU::executeThumb(uint16_t instruction) {
    uint8_t top5 = instruction >> 11;  // bits 15-11
    uint8_t top6 = instruction >> 10;  // bits 15-10
    uint8_t top8 = instruction >> 8;   // bits 15-8

    if ((top5 & 0b11111) == 0b00011)  // format 2 FIRST
        thumbAddSubtract(instruction);
    else if ((top5 & 0b11100) == 0b00000)  // format 1 AFTER
        thumbMoveShifted(instruction);
     else if ((top5 & 0b11100) == 0b00100)  // 001xx — format 3 move/compare/add/subtract immediate
        thumbMoveImmediate(instruction);
    else if (top6 == 0b010000)           // 010000 — format 4  thumbDataProcessing
        thumbDataProcessing(instruction);
    else if (top6 == 0b010001)           // 010001 — format 5 hi register/BX
        thumbHiRegister(instruction);
    else if ((top5 & 0b11111) == 0b01001)  // 01001 — format 6 PC-relative LDR
        thumbPCRelativeLDR(instruction);
    else if ((top5 & 0b11110) == 0b01010) {  // 0101x
        if ((instruction >> 9) & 1)
            thumbLoadStoreSign(instruction);   // format 8 — bit 9 = 1
        else
            thumbLoadStoreRegister(instruction); // format 7 — bit 9 = 0
    }
    else if ((top5 & 0b11100) == 0b01100)  // 011xx — format 9
        thumbLoadStoreImmediate(instruction);
    else if ((top5 & 0b11110) == 0b10000)  // 1000x — format 10 load/store halfword
        thumbLoadStoreHalfword(instruction);
    else if ((top5 & 0b11110) == 0b10010)  // 1001x — format 11 SP-relative
        thumbSPRelative(instruction);
    else if ((top5 & 0b11110) == 0b10100)  // 1010x — format 12 load address
        thumbLoadAddress(instruction);
    else if (top8 == 0b10110000 )  // 10110000 — format 13 add offset to SP
        thumbAddSP(instruction);
    else if ((top8 & 0b11110110) == 0b10110100)  // 1011x — format 14 push/pop
        thumbPushPop(instruction);
    else if ((top5 & 0b11110) == 0b11000)  // 1100x — format 15 multiple load/store
        thumbMultipleLoadStore(instruction);
    else if ((top5 & 0b11110) == 0b11010)  // 1101x — format 16/17 conditional branch/SWI
        thumbConditionalBranch(instruction);
    else if ((top5 & 0b11111) == 0b11100)  // 11100 — format 18 unconditional branch
        thumbUnconditionalBranch(instruction);
    else if (top5 == 0b11110) {
        // FIRST HALF — sets up LR
        // Formula: LR = PC + 4 + (nn << 12)
        // PC = address of first instruction = reg[15] - 2 (Step already incremented by 2)
        // so: LR = (reg[15] - 2) + 4 + (nn << 12) = reg[15] + 2 + (nn << 12)

        int32_t nn = instruction & 0x7FF;
        if (nn & (1 << 10)) nn |= 0xFFFFF800; // sign extend from 11 bits

        reg[14] = reg[15] + 2 + (nn << 12);
    }
    else if (top5 == 0b11111) {
        // SECOND HALF
        uint32_t nn = instruction & 0x7FF;
        uint32_t nextInstr = reg[15] | 1; // reg[15] is already PC+2

        // PC = LR + (Offset << 1)
        reg[15] = reg[14] + (nn << 1);
        // LR = Next Instruction Address | 1
        reg[14] = nextInstr;
    }
    else if (top5 == 0b11101) {
        // BLX — ARM9 only, not on GBA
        
    }
}