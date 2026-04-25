#include"cpu.h"

void CPU::switchMode(uint8_t newMode, bool returning) {
    uint8_t currentMode = cpsr & 0x1F;


    // step 1 — if returning, restore CPSR from current mode's SPSR first
    if (returning) {
        switch (currentMode) {
        case MODE_IRQ:        cpsr = spsr_irq; break;
        case MODE_SUPERVISOR: cpsr = spsr_svc; break;
        case MODE_FIQ:        cpsr = spsr_fiq; break;
        case MODE_ABORT:      cpsr = spsr_abt; break;
        case MODE_UNDEFINED:  cpsr = spsr_und; break;
        }
    }

    // step 2 — bank out current registers
    switch (currentMode) {
    case MODE_USER:
    case MODE_SYSTEM:
        break;
    case MODE_FIQ:
        reg_fiq[0] = reg[8];
        reg_fiq[1] = reg[9];
        reg_fiq[2] = reg[10];
        reg_fiq[3] = reg[11];
        reg_fiq[4] = reg[12];
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
    }

    // step 3 — if entering, save current CPSR into new mode's SPSR
    if (!returning) {
        switch (newMode) {
        case MODE_IRQ:        spsr_irq = cpsr; break;
        case MODE_SUPERVISOR: spsr_svc = cpsr; break;
        case MODE_FIQ:        spsr_fiq = cpsr; break;
        case MODE_ABORT:      spsr_abt = cpsr; break;
        case MODE_UNDEFINED:  spsr_und = cpsr; break;
        case MODE_USER:
        case MODE_SYSTEM:     break;
        }
    }

    // step 4 — load new mode's registers
    switch (newMode) {
    case MODE_USER:
    case MODE_SYSTEM:
        break;
    case MODE_FIQ:
        reg[8] = reg_fiq[0];
        reg[9] = reg_fiq[1];
        reg[10] = reg_fiq[2];
        reg[11] = reg_fiq[3];
        reg[12] = reg_fiq[4];
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
    }

    // step 5 — update CPSR mode bits (only when entering)
    if (!returning) {
        cpsr = (cpsr & ~0x1F) | newMode;
    }
}

void CPU::Step() {
    uint32_t pc = bus.read32(reg[15]);
    reg[15] += 4;
    uint32_t instruction = bus.read32(pc);
    Execute(instruction);
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
    bool link = (instruction >> 24) & 1; // L bit, 0=B 1=BL
    int32_t offset = instruction & 0xFFFFFF; // 24 bit offset

    if (offset & (1 << 23)) { // if signed — sign extend to 32 bits
        offset |= 0xFF000000;
    }

    if (link) { // BL — save return address in R14
        reg[14] = reg[15] - 4; //set R14 to PC (PC is 8 ahead, -4 gives instruction after branch)
    }

    reg[15] += (offset << 2); // shift left 2 (x4), since PC is 8 bytes ahead of branch
}

void CPU::executeBx(uint32_t instruction) {
    uint8_t Rn = instruction & 0xF; // operand register
    if (reg[Rn] & 1) { //bit 0=1? switch to thumb
        cpsr |= (1u << 5);        //SET T BIT — switch to THUMB mode
        reg[15] = reg[Rn] & ~1;   // clear bit 0 (it was a flag, not part of address)
    }
    else {
        cpsr &= ~(1u << 5);       // clear T bit — stay in ARM mode
        reg[15] = reg[Rn];        // jump to address
    }
}

uint32_t CPU::rotate_right(uint32_t value, uint8_t ammount) {
    if (ammount == 0) return value;
    return (value >> ammount) | (value << (32 - ammount));
}

uint32_t CPU::apply_shift(uint32_t value, uint8_t shift) {
    uint8_t type = (shift >> 1) & 0x3; //bit 1-2 — shift type
    uint8_t how = shift & 1;           // 1st bit — 0=immediate amount, 1=register amount

    uint8_t amount = { 0 };
    if (how == 0) { //plain number bit 7-3
        amount = (shift >> 3) & 0x1F;
    }
    else if (how == 1) { // from register
        uint8_t Rs = (shift >> 4) & 0xF; //bit 7-4
        amount = reg[Rs] & 0xFF;
    }

    switch (type) {
    case 0b00: return value << amount;                        //LSL — shift left
    case 0b01: if (amount == 0)return 0;                      //LSR — shift right
        return value >> amount;
    case 0b10: if (amount == 0) return (uint32_t)((int32_t)value >> 31); //ASR — arithmetic shift right (preserves sign)
        return (uint32_t)((int32_t)value >> amount);
    case 0b11:
        if (amount == 0) {  // RRX
            uint8_t oldC = (cpsr >> 29) & 1;
            return (oldC << 31) | (value >> 1);
        }
        return rotate_right(value, amount);            //ROR — rotate right
    }

    return value;
}

void CPU::setFlags(uint32_t result, uint64_t full, uint32_t Rn, uint32_t op2, bool isSub) {
    // N — negative
    (result >> 31)
        ? (cpsr |= (1u << 31))
        : (cpsr &= ~(1u << 31));

    // Z — zero
    (result == 0)
        ? (cpsr |= (1u << 30))
        : (cpsr &= ~(1u << 30));

    // C — carry
    bool C = isSub ? (Rn >= op2) : (full > 0xFFFFFFFF);
    C ? (cpsr |= (1u << 29)) : (cpsr &= ~(1u << 29));

    // V — signed overflow
    bool V = isSub
        ? (((Rn ^ op2) & (Rn ^ result)) >> 31)
        : ((~(Rn ^ op2) & (Rn ^ result)) >> 31);
    V ? (cpsr |= (1u << 28)) : (cpsr &= ~(1u << 28));
}

void CPU::executeDataProcessing(uint32_t instruction) {
    uint8_t immediate = (instruction >> 25) & 1; // immediate bit // =1? number value : register like R1
    uint8_t opcode = (instruction >> 21) & 0xF; // type of operation
    uint8_t Rd = (instruction >> 12) & 0xF; // which register to store into R0,R1....
    uint16_t oprand2 = instruction & 0xFFF; //
    uint8_t S = (instruction >> 20) & 1;

    switch (opcode) {

    case 0b0000: { // AND
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        reg[Rd] = reg[Rn] & op2;
        if (S && Rd != 15) setFlags(reg[Rd], (uint64_t)reg[Rd], reg[Rn], op2, false);
        break;
    }

    case 0b0001: { // EOR
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        reg[Rd] = reg[Rn] ^ op2;
        if (S && Rd != 15) setFlags(reg[Rd], (uint64_t)reg[Rd], reg[Rn], op2, false);
        break;
    }

    case 0b0010: { // SUB
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint64_t full = (uint64_t)reg[Rn] - op2;
        reg[Rd] = (uint32_t)full;
        if (S && Rd != 15) setFlags(reg[Rd], full, reg[Rn], op2, true);
        break;
    }

    case 0b0011: { // RSB (reverse subtract — op2 - Rn)
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint64_t full = (uint64_t)op2 - reg[Rn];
        reg[Rd] = (uint32_t)full;
        if (S && Rd != 15) setFlags(reg[Rd], full, op2, reg[Rn], true); // note: op2 and Rn swapped — op2 is the minuend here
        break;
    }

    case 0b0100: { // ADD
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint64_t full = (uint64_t)reg[Rn] + op2;
        reg[Rd] = (uint32_t)full;
        if (S && Rd != 15) setFlags(reg[Rd], full, reg[Rn], op2, false);
        break;
    }

    case 0b0101: { // ADC (add with carry)
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint8_t carry = (cpsr >> 29) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint64_t full = (uint64_t)reg[Rn] + op2 + carry;
        reg[Rd] = (uint32_t)full;
        if (S && Rd != 15) setFlags(reg[Rd], full, reg[Rn], op2, false);
        break;
    }

    case 0b0110: { // SBC (subtract with carry)
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint8_t carry = (cpsr >> 29) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint64_t full = (uint64_t)reg[Rn] - op2 + carry - 1;
        reg[Rd] = (uint32_t)full;
        if (S && Rd != 15) setFlags(reg[Rd], full, reg[Rn], op2, true);
        break;
    }

    case 0b0111: { // RSC (reverse subtract with carry)
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint8_t carry = (cpsr >> 29) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint64_t full = (uint64_t)op2 - reg[Rn] + carry - 1;
        reg[Rd] = (uint32_t)full;
        if (S && Rd != 15) setFlags(reg[Rd], full, op2, reg[Rn], true); // op2 and Rn swapped — op2 is the minuend
        break;
    }

    case 0b1000: { // TST (AND, flags only)
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint32_t result = reg[Rn] & op2;
        setFlags(result, (uint64_t)result, reg[Rn], op2, false);
        break;
    }

    case 0b1001: { // TEQ (EOR, flags only)
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint32_t result = reg[Rn] ^ op2;
        setFlags(result, (uint64_t)result, reg[Rn], op2, false);
        break;
    }

    case 0b1010: { // CMP (SUB, flags only)
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint64_t full = (uint64_t)reg[Rn] - op2;
        uint32_t result = (uint32_t)full;
        setFlags(result, full, reg[Rn], op2, true);
        break;
    }

    case 0b1011: { // CMN (ADD, flags only)
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        uint64_t full = (uint64_t)reg[Rn] + op2;
        uint32_t result = (uint32_t)full;
        setFlags(result, full, reg[Rn], op2, false);
        break;
    }

    case 0b1100: { // ORR
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        reg[Rd] = reg[Rn] | op2;
        if (S && Rd != 15) setFlags(reg[Rd], (uint64_t)reg[Rd], reg[Rn], op2, false);
        break;
    }

    case 0b1101: { // MOV
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        reg[Rd] = op2;
        if (S && Rd != 15) setFlags(reg[Rd], (uint64_t)reg[Rd], 0, op2, false);
        break;
    }

    case 0b1110: { // BIC
        uint8_t Rn = (instruction >> 16) & 0xF;
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        reg[Rd] = reg[Rn] & ~op2;
        if (S && Rd != 15) setFlags(reg[Rd], (uint64_t)reg[Rd], reg[Rn], op2, false);
        break;
    }

    case 0b1111: { // MVN (NOT op2)
        uint8_t S = (instruction >> 20) & 1;
        uint32_t op2;
        if (immediate) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm8 = instruction & 0xFF;
            op2 = rotate_right(imm8, rotation * 2);
        }
        else {
            uint8_t rm = instruction & 0xF;
            uint8_t shift = (instruction >> 4) & 0xFF;
            op2 = apply_shift(reg[rm], shift);
        }
        reg[Rd] = ~op2;
        if (S && Rd != 15) setFlags(reg[Rd], (uint64_t)reg[Rd], 0, op2, false);
        break;
    }

    }

    if (Rd == 15 && S) {
        //TST=1000, TEQ=1001, CMP=1010, CMN=1011 don't write to Rd
        if (opcode < 0b1000 || opcode>0b1011) {
            uint8_t currentMode = cpsr & 0x1F;
            uint8_t newMode;

            switch (currentMode) {
            case MODE_IRQ: newMode = spsr_irq & 0x1F;break;
            case MODE_FIQ: newMode = spsr_fiq & 0x1F;break;
            case MODE_SUPERVISOR: newMode = spsr_svc & 0x1F;break;
            case MODE_UNDEFINED: newMode = spsr_und & 0x1F;break;
            case MODE_ABORT: newMode = spsr_abt & 0x1F;break;
            default: newMode = MODE_USER; break;
            }

            switchMode(newMode, true);
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
        reg[Rd] = reg[Rm] * reg[Rs] + reg[Rn];
    }
    else  //MLU
        reg[Rd] = reg[Rm] * reg[Rs];

    if (S) { //only N and Z matters in here
        (reg[Rd] >> 31) ? (cpsr |= (1u << 31)) : (cpsr &= ~(1u << 31));  // N
        (reg[Rd] == 0) ? (cpsr |= (1u << 30)) : (cpsr &= ~(1u << 30));  // Z
    }
}

void CPU::executeMultiplyLong(uint32_t instruction) {
    uint8_t U = (instruction >> 22) & 1;//1?signed: unsigned
    uint8_t A = (instruction >> 21) & 1;//1?acumulate:don't
    uint8_t S = (instruction >> 20) & 1;
    uint8_t RdHi = (instruction >> 16) & 0xF;
    uint8_t RdLo = (instruction >> 12) & 0xF;
    uint8_t Rs = (instruction >> 8) & 0xF;
    uint8_t Rm = instruction & 0xF;


    uint64_t result;

    if (U) {//signed
        int64_t sresult = (int64_t)(int32_t)reg[Rm] * (int64_t)(int32_t)reg[Rs]; //cast to int32_t first to save the sign
        if (A) {
            int64_t acc = ((int64_t)reg[RdHi] << 32) | (int32_t)reg[RdLo];
            sresult += acc;
        }
        result = sresult;

    }
    else {
        uint64_t sresult = (uint64_t)(reg[Rm]) * (uint64_t)(reg[Rs]);
        if (A) {
            uint64_t acc = ((uint64_t)reg[RdHi] << 32) | reg[RdLo];
            sresult += acc;
        }
        result = sresult;
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
        offset = apply_shift(reg[Rm], shift);   // register + shift
    }

    // step 2 — calculate the full address (add or subtract offset)
    uint32_t address = U ? (reg[Rn] + offset) : (reg[Rn] - offset);

    // step 3 — pick effective address (pre: use offset, post: use base)
    uint32_t effective = P ? address : reg[Rn];

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
            bus.write32(effective, reg[Rd]);                   // STR  — 4 bytes
        }
    }

    // step 5 — writeback
    // post-index always writes back, pre-index only if W=1
    if (!P || W) {
        reg[Rn] = address;
    }
}

void CPU::executeHalfwordTransfer(uint32_t instruction) {
    uint8_t P = (instruction >> 24) & 1;
    uint8_t U = (instruction >> 23) & 1;
    uint8_t I = (instruction >> 22) & 1; // 0=register offset, 1=immediate offset
    uint8_t W = (instruction >> 21) & 1;
    uint8_t L = (instruction >> 20) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;
    uint8_t Rd = (instruction >> 12) & 0xF;
    uint8_t SH = (instruction >> 5) & 0x3;



    uint32_t offset;
    if (I) {
        // immediate — split into high and low nibble
        uint8_t immHi = (instruction >> 8) & 0xF; // bits 11-8
        uint8_t immLo = instruction & 0xF;         // bits 3-0
        offset = (immHi << 4) | immLo;
    }
    else {
        // register
        uint8_t Rm = instruction & 0xF;
        offset = reg[Rm];
    }


    // step 2 — calculate address
    uint32_t address = U ? (reg[Rn] + offset) : (reg[Rn] - offset);
    uint32_t effective = P ? address : reg[Rn];

    // step 3 — do the transfer
    if (L) {
        switch (SH) {
        case 0b01: {
            // LDRH — unsigned halfword, zero-extend
            reg[Rd] = bus.read16(effective);
            break;
        }
        case 0b10: {
            // LDRSB — signed byte, sign-extend
            uint32_t value = bus.read8(effective);
            if (value & 0x80) value |= 0xFFFFFF00;
            reg[Rd] = value;
            break;
        }
        case 0b11: {
            // LDRSH — signed halfword, sign-extend
            uint32_t value = bus.read16(effective);
            if (value & 0x8000) value |= 0xFFFF0000;
            reg[Rd] = value;
            break;
        }
        }
    }
    else {
        // STRH only
        if (SH == 0b01) {
            bus.write16(effective, reg[Rd] & 0xFFFF);
        }
    }

    // step 4 — writeback
    if (!P || W) {
        reg[Rn] = address;
    }
}

void CPU::executeSWP(uint32_t instruction) {
    uint8_t B = (instruction >> 22) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;  // address
    uint8_t Rd = (instruction >> 12) & 0xF;  // destination
    uint8_t Rm = instruction & 0xF;           // source

    if (B) {
        // SWPB — byte
        uint8_t temp = bus.read8(reg[Rn]);
        bus.write8(reg[Rn], reg[Rm] & 0xFF);
        reg[Rd] = temp;
    }
    else {
        // SWP — word
        uint32_t temp = bus.read32(reg[Rn]);
        bus.write32(reg[Rn], reg[Rm]);
        reg[Rd] = temp;
    }
}

void CPU::executeLoadStoreMultiple(uint32_t instruction) {
    uint8_t P = (instruction >> 24) & 1;
    uint8_t U = (instruction >> 23) & 1;
    uint8_t W = (instruction >> 21) & 1;
    uint8_t L = (instruction >> 20) & 1;
    uint8_t Rn = (instruction >> 16) & 0xF;
    uint16_t regList = instruction & 0xFFFF;

    // count registers
    int count = 0;
    for (int i = 0; i < 16; i++)
        if (regList & (1 << i)) count++;

    uint32_t address = reg[Rn];

    // figure out starting address
    if (U) {
        // going UP
        if (P) address += 4;        // pre-increment — adjust before first transfer
    }
    else {

        address -= count * 4;
        if (!P) address += 4;       // post-decrement — first transfer at Rn, not Rn-4
    }

    // always iterate registers low to high — lowest reg goes to lowest address
    for (int i = 0; i < 16; i++) {
        if (regList & (1 << i)) {
            if (L) reg[i] = bus.read32(address); // LOAD
            else   bus.write32(address, reg[i]); // STORE
            address += 4; // always move up through memory
        }
    }

    // writeback
    if (W) {
        if (U) reg[Rn] = address - (P ? 0 : 4); // end of range
        else   reg[Rn] = reg[Rn] - count * 4;    // bottom of range
    }

}

void CPU::executePSRTransfer(uint32_t instruction) {
    uint8_t Pd = (instruction >> 22) & 1; //1?SPSR:CPSR
    uint8_t op = (instruction >> 21) & 1; //1?MSR :MRS
    uint8_t I = (instruction >> 25) & 1;
    if (op) { //MSR
        uint32_t value;
        if (I) {
            uint8_t rotation = (instruction >> 8) & 0xF;
            uint8_t imm = (instruction & 0xFF);
            value = rotate_right(imm, rotation * 2);
        }
        else {
            uint8_t Rm = instruction & 0xF;
            value = reg[Rm];
        }
        uint32_t mask = { 0 };
        if ((instruction & 0x00080000)) mask |= 0xFF000000; //flags
        if ((instruction & 0x00040000)) mask |= 0x00FF0000; //status
        if ((instruction & 0x00020000)) mask |= 0x0000FF00; //extension
        if ((instruction & 0x00010000)) mask |= 0x000000FF; //control

        uint8_t currentMode = cpsr & 0x1F;
        if (currentMode == MODE_USER) {
            mask &= 0xFF000000;
        }
        if (Pd) { //SPSR

            switch (currentMode) {
            case MODE_IRQ: spsr_irq = (spsr_irq & ~mask) | (value & mask);break;
            case MODE_FIQ:  spsr_fiq = (spsr_fiq & ~mask) | (value & mask);break;
            case MODE_SUPERVISOR:  spsr_svc = (spsr_svc & ~mask) | (value & mask);break;
            case MODE_UNDEFINED:  spsr_und = (spsr_und & ~mask) | (value & mask);break;
            case MODE_ABORT:  spsr_abt = (spsr_abt & ~mask) | (value & mask);break;

            }

        }
        else {
            uint8_t oldMode = cpsr & 0x1F;
            cpsr = (cpsr & ~mask) | (value & mask);
            uint8_t currentMode = cpsr & 0x1F;
            if (oldMode != currentMode) { //change in mode detected
                switchMode(currentMode, false);
            }
        }
    }
    else { //MRS

        uint8_t Rd = (instruction >> 12) & 0xF;
        if (Pd) {
            uint8_t currentMode = cpsr & 0x1F;
            switch (currentMode) {
            case MODE_IRQ:  reg[Rd] = spsr_irq;break;
            case MODE_FIQ:  reg[Rd] = spsr_fiq;break;
            case MODE_SUPERVISOR:  reg[Rd] = spsr_svc;break;
            case MODE_UNDEFINED:  reg[Rd] = spsr_und;break;
            case MODE_ABORT:  reg[Rd] = spsr_abt;break;

            }
        }
        else {
            reg[Rd] = cpsr;
        }
    }

}

void CPU::executeSWI(uint32_t instruction) {
    uint32_t returnAddress = reg[15] - 4;    // calculate before anything changes
    switchMode(MODE_SUPERVISOR, false);       // spsr_svc = old CPSR, loads svc regs
    reg[14] = returnAddress;                  // R14_svc = return address
    cpsr |= (1 << 7);                         // IRQ disabled
    cpsr &= ~(1 << 5);                        // ARM mode
    reg[15] = 0x00000008;                     // jump to handler
}

void CPU::triggerIRQ() {
    // only trigger if IRQs are enabled
    if (cpsr & (1u << 7)) return; // I bit set = disabled

    reg[14] = reg[15] - 4;        // save return address in R14_irq
    switchMode(MODE_IRQ, false);   // spsr_irq = old CPSR, loads irq regs
    cpsr |= (1u << 7);           // disable further IRQs
    cpsr &= ~(1u << 5);           // force ARM mode
    reg[15] = 0x00000018;         // jump to IRQ handler
}

void CPU::Execute(uint32_t instruction) {
    //decoding instruction
    uint8_t condition = (instruction >> 28); //fectching condition
    if (!checkCondition(condition)) return;

    // BX must be checked first — unique bit pattern 0x012FFF10
    if ((instruction & 0x0FFFFFF0) == 0x012FFF10) {
        executeBx(instruction);
        return;
    }

    uint8_t type = (instruction >> 25) & 0x7; //  instruction type

    switch (type) {
    case 0b101: // branch B or BL 
        executeBranch(instruction);
        break;

    case 0b000:
    case 0b001:
        if ((instruction & 0x0FB00FF0) == 0x01000090) { // SWP pattern 
            executeSWP(instruction);
        }
        else if (((instruction & 0b10010000) == 0b10010000) && ((instruction >> 5) & 0x3) != 0) {
            executeHalfwordTransfer(instruction);
        }
        else if (((instruction >> 22) & 0x3F) == 0 && ((instruction >> 4) & 0xF) == 0b1001) {
            executeMultiply(instruction);
        }
        else if (((instruction >> 23) & 0x1F) == 1 && ((instruction >> 4) & 0xF) == 0b1001) {
            executeMultiplyLong(instruction);
        }
        else {
            executeDataProcessing(instruction);
        }
        break;
    case 0b010: // load/store
    case 0b011: // load/store
        executeLoadStore(instruction);
        break;
    case 0b100: // LDM/STM multiple register transfer
        executeLoadStoreMultiple(instruction);
        break;
    case 0b111: // SWI software interrupt
        executeSWI(instruction);
        break;
    }
}