// Disassembler.h
// Compact ARM7TDMI disassembler (ARM + Thumb) for debugger display.
// Not cycle-accurate, not exhaustive — aims to cover the instructions
// you'll actually see in GBA game/BIOS code, with readable Ghidra-style
// mnemonics (condition suffix attached, e.g. ANDLE, BNE, BLEQ).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

class Disassembler {
public:
    // Decodes one instruction at `addr` (Thumb if thumbMode, else ARM).
    // `read16`/`read32` are callables: uint16_t(uint32_t) / uint32_t(uint32_t)
    // matching Bus::read16 / Bus::read32. Writes result into `out` (caller-sized buffer)
    // and returns the instruction length in bytes (2 or 4).
    template <typename Read16Fn, typename Read32Fn>
    static int Decode(uint32_t addr, bool thumbMode, Read16Fn read16, Read32Fn read32,
        char* out, size_t outSize) {
        if (thumbMode) {
            uint16_t instr = read16(addr);
            decodeThumb(addr, instr, out, outSize);
            return 2;
        }
        else {
            uint32_t instr = read32(addr);
            decodeARM(addr, instr, out, outSize);
            return 4;
        }
    }

private:
    static const char* condName(uint8_t cond) {
        static const char* names[16] = {
            "EQ","NE","CS","CC","MI","PL","VS","VC",
            "HI","LS","GE","LT","GT","LE","","NV"
        };
        return names[cond & 0xF];
    }
    static const char* regName(uint8_t r) {
        static const char* names[16] = {
            "R0","R1","R2","R3","R4","R5","R6","R7",
            "R8","R9","R10","R11","R12","SP","LR","PC"
        };
        return names[r & 0xF];
    }
    static const char* shiftName(uint8_t t) {
        static const char* n[4] = { "LSL","LSR","ASR","ROR" };
        return n[t & 0x3];
    }

    // ----------------------------------------------------------------
    // ARM (32-bit) decode
    // ----------------------------------------------------------------
    static void decodeARM(uint32_t addr, uint32_t instr, char* out, size_t outSize) {
        uint8_t cond = (instr >> 28) & 0xF;
        const char* cc = condName(cond);

        // BX
        if ((instr & 0x0FFFFFF0) == 0x012FFF10) {
            snprintf(out, outSize, "BX%s    %s", cc, regName(instr & 0xF));
            return;
        }
        // Branch / Branch+Link
        if (((instr >> 25) & 0x7) == 0b101) {
            bool link = (instr >> 24) & 1;
            int32_t off = instr & 0xFFFFFF;
            if (off & 0x800000) off |= 0xFF000000;
            uint32_t target = addr + 8 + (off << 2);
            snprintf(out, outSize, "%s%s%s  0x%08X", link ? "BL" : "B", cc,
                link ? "" : "  ", target);
            return;
        }
        // SWI
        if (((instr >> 25) & 0x7) == 0b111) {
            snprintf(out, outSize, "SWI%s   #0x%X", cc, instr & 0xFFFFFF);
            return;
        }
        // Block data transfer (LDM/STM)
        if (((instr >> 25) & 0x7) == 0b100) {
            bool L = (instr >> 20) & 1;
            bool U = (instr >> 23) & 1;
            bool P = (instr >> 24) & 1;
            bool W = (instr >> 21) & 1;
            uint8_t Rn = (instr >> 16) & 0xF;
            char mode[4] = "IA";
            if (P && U)       memcpy(mode, "IB", 2);
            else if (!P && U) memcpy(mode, "IA", 2);
            else if (P && !U) memcpy(mode, "DB", 2);
            else              memcpy(mode, "DA", 2);
            char regs[80] = { 0 };
            int rlen = 0;
            uint16_t list = instr & 0xFFFF;
            for (int i = 0; i < 16 && rlen < 70; i++) {
                if (list & (1 << i)) {
                    rlen += snprintf(regs + rlen, sizeof(regs) - rlen,
                        rlen ? ",%s" : "%s", regName(i));
                }
            }
            snprintf(out, outSize, "%s%s%s %s%s,{%s}", L ? "LDM" : "STM", mode, cc,
                regName(Rn), W ? "!" : "", regs);
            return;
        }
        // Single data transfer (LDR/STR)
        if (((instr >> 26) & 0x3) == 0b01) {
            bool I = (instr >> 25) & 1;
            bool P = (instr >> 24) & 1;
            bool U = (instr >> 23) & 1;
            bool B = (instr >> 22) & 1;
            bool W = (instr >> 21) & 1;
            bool L = (instr >> 20) & 1;
            uint8_t Rn = (instr >> 16) & 0xF;
            uint8_t Rd = (instr >> 12) & 0xF;
            char offStr[40];
            if (!I) {
                uint32_t imm = instr & 0xFFF;
                snprintf(offStr, sizeof(offStr), "#%s0x%X", U ? "" : "-", imm);
            }
            else {
                uint8_t Rm = instr & 0xF;
                uint8_t shift = (instr >> 4) & 0xFF;
                uint8_t shAmt = (shift >> 3) & 0x1F;
                uint8_t shType = (shift >> 1) & 0x3;
                if (shAmt == 0)
                    snprintf(offStr, sizeof(offStr), "%s%s", U ? "" : "-", regName(Rm));
                else
                    snprintf(offStr, sizeof(offStr), "%s%s,%s #0x%X", U ? "" : "-",
                        regName(Rm), shiftName(shType), shAmt);
            }
            if (P)
                snprintf(out, outSize, "%s%s%s %s,[%s,%s]%s", L ? "LDR" : "STR", B ? "B" : "",
                    cc, regName(Rd), regName(Rn), offStr, W ? "!" : "");
            else
                snprintf(out, outSize, "%s%s%s %s,[%s],%s", L ? "LDR" : "STR", B ? "B" : "",
                    cc, regName(Rd), regName(Rn), offStr);
            return;
        }
        // Halfword/signed transfer
        if (((instr >> 25) & 0x7) == 0 && ((instr >> 7) & 1) && ((instr >> 4) & 1) &&
            ((instr >> 5) & 0x3) != 0) {
            bool P = (instr >> 24) & 1;
            bool U = (instr >> 23) & 1;
            bool Iimm = (instr >> 22) & 1;
            bool L = (instr >> 20) & 1;
            uint8_t Rn = (instr >> 16) & 0xF;
            uint8_t Rd = (instr >> 12) & 0xF;
            uint8_t SH = (instr >> 5) & 0x3;
            const char* op = (SH == 1) ? "H" : (SH == 2) ? "SB" : "SH";
            char offStr[32];
            if (Iimm) {
                uint8_t hi = (instr >> 8) & 0xF, lo = instr & 0xF;
                snprintf(offStr, sizeof(offStr), "#%s0x%X", U ? "" : "-", (hi << 4) | lo);
            }
            else {
                snprintf(offStr, sizeof(offStr), "%s%s", U ? "" : "-", regName(instr & 0xF));
            }
            if (P)
                snprintf(out, outSize, "%s%s%s %s,[%s,%s]", L ? "LDR" : "STR", op, cc,
                    regName(Rd), regName(Rn), offStr);
            else
                snprintf(out, outSize, "%s%s%s %s,[%s],%s", L ? "LDR" : "STR", op, cc,
                    regName(Rd), regName(Rn), offStr);
            return;
        }
        // Multiply / Multiply-long
        if (((instr >> 22) & 0x3F) == 0 && ((instr & 0xF0) == 0x90)) {
            bool A = (instr >> 21) & 1;
            uint8_t Rd = (instr >> 16) & 0xF, Rn = (instr >> 12) & 0xF;
            uint8_t Rs = (instr >> 8) & 0xF, Rm = instr & 0xF;
            if (A)
                snprintf(out, outSize, "MLA%s   %s,%s,%s,%s", cc, regName(Rd), regName(Rm),
                    regName(Rs), regName(Rn));
            else
                snprintf(out, outSize, "MUL%s   %s,%s,%s", cc, regName(Rd), regName(Rm), regName(Rs));
            return;
        }
        // MRS/MSR (PSR transfer)
        if ((instr & 0x0FBF0FFF) == 0x010F0000) {
            bool Pd = (instr >> 22) & 1;
            uint8_t Rd = (instr >> 12) & 0xF;
            snprintf(out, outSize, "MRS%s   %s,%s", cc, regName(Rd), Pd ? "SPSR" : "CPSR");
            return;
        }
        if ((instr & 0x0DB0F000) == 0x0120F000 || ((instr >> 25 & 1) && (instr & 0x0FB0F000) == 0x0320F000)) {
            bool Pd = (instr >> 22) & 1;
            bool I = (instr >> 25) & 1;
            char src[24];
            if (I) {
                uint8_t rot = (instr >> 8) & 0xF, imm = instr & 0xFF;
                uint32_t val = (imm >> (rot * 2)) | (imm << (32 - rot * 2));
                snprintf(src, sizeof(src), "#0x%X", rot ? val : imm);
            }
            else {
                snprintf(src, sizeof(src), "%s", regName(instr & 0xF));
            }
            snprintf(out, outSize, "MSR%s   %s,%s", cc, Pd ? "SPSR" : "CPSR", src);
            return;
        }
        // SWP
        if ((instr & 0x0FB00FF0) == 0x01000090) {
            bool B = (instr >> 22) & 1;
            uint8_t Rn = (instr >> 16) & 0xF, Rd = (instr >> 12) & 0xF, Rm = instr & 0xF;
            snprintf(out, outSize, "SWP%s%s  %s,%s,[%s]", B ? "B" : "", cc, regName(Rd),
                regName(Rm), regName(Rn));
            return;
        }
        // Data processing (catch-all)
        {
            bool I = (instr >> 25) & 1;
            uint8_t opcode = (instr >> 21) & 0xF;
            bool S = (instr >> 20) & 1;
            uint8_t Rn = (instr >> 16) & 0xF, Rd = (instr >> 12) & 0xF;
            static const char* ops[16] = {
                "AND","EOR","SUB","RSB","ADD","ADC","SBC","RSC",
                "TST","TEQ","CMP","CMN","ORR","MOV","BIC","MVN"
            };
            bool noRd = (opcode == 8 || opcode == 9 || opcode == 0xA || opcode == 0xB); // TST/TEQ/CMP/CMN
            bool noRn = (opcode == 0xD || opcode == 0xF); // MOV/MVN
            char op2[40];
            if (I) {
                uint8_t rot = (instr >> 8) & 0xF, imm = instr & 0xFF;
                uint32_t val = rot ? ((imm >> (rot * 2)) | (imm << (32 - rot * 2))) : imm;
                snprintf(op2, sizeof(op2), "#0x%X", val);
            }
            else {
                uint8_t Rm = instr & 0xF;
                uint8_t shiftField = (instr >> 4) & 0xFF;
                bool regShift = shiftField & 1;
                uint8_t shType = (shiftField >> 1) & 0x3;
                if (regShift) {
                    uint8_t Rs = (shiftField >> 4) & 0xF;
                    snprintf(op2, sizeof(op2), "%s,%s %s", regName(Rm), shiftName(shType), regName(Rs));
                }
                else {
                    uint8_t shAmt = (shiftField >> 3) & 0x1F;
                    if (shAmt == 0 && shType == 0)
                        snprintf(op2, sizeof(op2), "%s", regName(Rm));
                    else
                        snprintf(op2, sizeof(op2), "%s,%s #0x%X", regName(Rm), shiftName(shType), shAmt);
                }
            }
            char buf[96];
            if (noRd)
                snprintf(buf, sizeof(buf), "%s%s   %s,%s", ops[opcode], cc, regName(Rn), op2);
            else if (noRn)
                snprintf(buf, sizeof(buf), "%s%s%s  %s,%s", ops[opcode], cc, S ? "S" : "", regName(Rd), op2);
            else
                snprintf(buf, sizeof(buf), "%s%s%s  %s,%s,%s", ops[opcode], cc, S ? "S" : "",
                    regName(Rd), regName(Rn), op2);
            snprintf(out, outSize, "%s", buf);
            return;
        }
    }

    // ----------------------------------------------------------------
    // Thumb (16-bit) decode
    // ----------------------------------------------------------------
    static void decodeThumb(uint32_t addr, uint16_t instr, char* out, size_t outSize) {
        uint8_t top5 = instr >> 11;
        uint8_t top6 = instr >> 10;
        uint8_t top8 = instr >> 8;

        // Move shifted register (LSL/LSR/ASR #imm) — top3 bits 000, but not add/sub (op field != 11)
        if ((instr >> 13) == 0b000 && ((instr >> 11) & 0x3) != 0b11) {
            uint8_t op = (instr >> 11) & 0x3;
            uint8_t offset = (instr >> 6) & 0x1F;
            uint8_t Rs = (instr >> 3) & 0x7, Rd = instr & 0x7;
            static const char* names[3] = { "LSL","LSR","ASR" };
            snprintf(out, outSize, "%s    %s,%s,#0x%X", names[op], regName(Rd), regName(Rs), offset);
            return;
        }
        // Add/subtract
        if ((instr >> 11) == 0b00011) {
            bool I = (instr >> 10) & 1;
            bool op = (instr >> 9) & 1;
            uint8_t Rn_or_imm = (instr >> 6) & 0x7;
            uint8_t Rs = (instr >> 3) & 0x7, Rd = instr & 0x7;
            if (I)
                snprintf(out, outSize, "%s    %s,%s,#0x%X", op ? "SUB" : "ADD", regName(Rd), regName(Rs), Rn_or_imm);
            else
                snprintf(out, outSize, "%s    %s,%s,%s", op ? "SUB" : "ADD", regName(Rd), regName(Rs), regName(Rn_or_imm));
            return;
        }
        // Move/compare/add/subtract immediate
        if ((top5 & 0b11100) == 0b00100) {
            uint8_t op = (instr >> 11) & 0x3;
            uint8_t Rd = (instr >> 8) & 0x7;
            uint8_t off = instr & 0xFF;
            static const char* names[4] = { "MOV","CMP","ADD","SUB" };
            snprintf(out, outSize, "%s    %s,#0x%X", names[op], regName(Rd), off);
            return;
        }
        // ALU operations
        if (top6 == 0b010000) {
            uint8_t op = (instr >> 6) & 0xF;
            uint8_t Rs = (instr >> 3) & 0x7, Rd = instr & 0x7;
            static const char* names[16] = {
                "AND","EOR","LSL","LSR","ASR","ADC","SBC","ROR",
                "TST","NEG","CMP","CMN","ORR","MUL","BIC","MVN"
            };
            snprintf(out, outSize, "%s    %s,%s", names[op], regName(Rd), regName(Rs));
            return;
        }
        // Hi register operations / BX
        if (top6 == 0b010001) {
            uint8_t op = (instr >> 8) & 0x3;
            uint8_t rdLow = instr & 0x7, MSBd = (instr >> 7) & 1;
            uint8_t Rd = rdLow | (MSBd << 3);
            uint8_t Rs = (instr >> 3) & 0xF;
            static const char* names[4] = { "ADD","CMP","MOV","BX" };
            if (op == 3)
                snprintf(out, outSize, "BX     %s", regName(Rs));
            else
                snprintf(out, outSize, "%s    %s,%s", names[op], regName(Rd), regName(Rs));
            return;
        }
        // PC-relative load
        if ((top5 & 0b11111) == 0b01001) {
            uint8_t Rd = (instr >> 8) & 0x7;
            uint8_t word8 = instr & 0xFF;
            uint32_t target = ((addr + 4) & ~2u) + word8 * 4;
            snprintf(out, outSize, "LDR    %s,[PC,#0x%X] ; =0x%08X", regName(Rd), word8 * 4, target);
            return;
        }
        // Load/store with register offset
        if ((top5 & 0b11110) == 0b01010 && !((instr >> 9) & 1)) {
            uint8_t op = (instr >> 10) & 0x3;
            uint8_t R0 = (instr >> 6) & 0x7, Rb = (instr >> 3) & 0x7, Rd = instr & 0x7;
            static const char* names[4] = { "STR","STRB","LDR","LDRB" };
            snprintf(out, outSize, "%s   %s,[%s,%s]", names[op], regName(Rd), regName(Rb), regName(R0));
            return;
        }
        // Load/store sign-extended
        if ((top5 & 0b11110) == 0b01010 && ((instr >> 9) & 1)) {
            uint8_t op = (instr >> 10) & 0x3;
            uint8_t R0 = (instr >> 6) & 0x7, Rb = (instr >> 3) & 0x7, Rd = instr & 0x7;
            static const char* names[4] = { "STRH","LDSB","LDRH","LDSH" };
            snprintf(out, outSize, "%s   %s,[%s,%s]", names[op], regName(Rd), regName(Rb), regName(R0));
            return;
        }
        // Load/store immediate offset
        if ((top5 & 0b11100) == 0b01100) {
            uint8_t op = (instr >> 11) & 0x3;
            uint8_t offset = (instr >> 6) & 0x1F;
            uint8_t Rb = (instr >> 3) & 0x7, Rd = instr & 0x7;
            static const char* names[4] = { "STR","LDR","STRB","LDRB" };
            uint32_t imm = (op < 2) ? offset * 4 : offset;
            snprintf(out, outSize, "%s    %s,[%s,#0x%X]", names[op], regName(Rd), regName(Rb), imm);
            return;
        }
        // Load/store halfword
        if ((top5 & 0b11110) == 0b10000) {
            bool L = (instr >> 11) & 1;
            uint8_t nn = (instr >> 6) & 0x1F;
            uint8_t Rb = (instr >> 3) & 0x7, Rd = instr & 0x7;
            snprintf(out, outSize, "%s   %s,[%s,#0x%X]", L ? "LDRH" : "STRH", regName(Rd), regName(Rb), nn * 2);
            return;
        }
        // SP-relative load/store
        if ((top5 & 0b11110) == 0b10010) {
            bool L = (instr >> 11) & 1;
            uint8_t Rd = (instr >> 8) & 0x7;
            uint8_t nn = instr & 0xFF;
            snprintf(out, outSize, "%s    %s,[SP,#0x%X]", L ? "LDR" : "STR", regName(Rd), nn * 4);
            return;
        }
        // Load address
        if ((top5 & 0b11110) == 0b10100) {
            bool sp = (instr >> 11) & 1;
            uint8_t Rd = (instr >> 8) & 0x7;
            uint8_t nn = instr & 0xFF;
            snprintf(out, outSize, "ADD    %s,%s,#0x%X", regName(Rd), sp ? "SP" : "PC", nn * 4);
            return;
        }
        // Add offset to SP
        if (top8 == 0b10110000) {
            bool neg = (instr >> 7) & 1;
            uint8_t nn = instr & 0x7F;
            snprintf(out, outSize, "ADD    SP,#%s0x%X", neg ? "-" : "", nn * 4);
            return;
        }
        // Push/pop
        if ((top8 & 0b11110110) == 0b10110100) {
            bool L = (instr >> 11) & 1;
            bool R = (instr >> 8) & 1;
            uint8_t list = instr & 0xFF;
            char regs[64] = { 0 };
            int rlen = 0;
            for (int i = 0; i < 8; i++)
                if (list & (1 << i))
                    rlen += snprintf(regs + rlen, sizeof(regs) - rlen, rlen ? ",%s" : "%s", regName(i));
            if (R) rlen += snprintf(regs + rlen, sizeof(regs) - rlen, rlen ? ",%s" : "%s", L ? "PC" : "LR");
            snprintf(out, outSize, "%s   {%s}", L ? "POP" : "PUSH", regs);
            return;
        }
        // Multiple load/store
        if ((top5 & 0b11110) == 0b11000) {
            bool L = (instr >> 11) & 1;
            uint8_t Rb = (instr >> 8) & 0x7;
            uint8_t list = instr & 0xFF;
            char regs[64] = { 0 };
            int rlen = 0;
            for (int i = 0; i < 8; i++)
                if (list & (1 << i))
                    rlen += snprintf(regs + rlen, sizeof(regs) - rlen, rlen ? ",%s" : "%s", regName(i));
            snprintf(out, outSize, "%s  %s!,{%s}", L ? "LDMIA" : "STMIA", regName(Rb), regs);
            return;
        }
        // SWI
        if (top8 == 0b11011111) {
            snprintf(out, outSize, "SWI    #0x%X", instr & 0xFF);
            return;
        }
        // Conditional branch
        if ((top5 & 0b11110) == 0b11010) {
            uint8_t cond = (instr >> 8) & 0xF;
            int8_t off = (int8_t)(instr & 0xFF);
            uint32_t target = addr + 4 + (off << 1);
            snprintf(out, outSize, "B%-3s   0x%08X", condName(cond), target);
            return;
        }
        // Unconditional branch
        if ((top5 & 0b11111) == 0b11100) {
            int32_t off = instr & 0x7FF;
            if (off & 0x400) off |= ~0x7FF;
            uint32_t target = addr + 4 + (off << 1);
            snprintf(out, outSize, "B      0x%08X", target);
            return;
        }
        // Long branch with link (both halves)
        if (top5 == 0b11110) {
            int32_t nn = instr & 0x7FF;
            if (nn & (1 << 10)) nn |= 0xFFFFF800;
            snprintf(out, outSize, "BL     0x%08X (hi)", addr + 4 + (nn << 12));
            return;
        }
        if (top5 == 0b11111) {
            uint32_t nn = instr & 0x7FF;
            snprintf(out, outSize, "BL     +0x%X (lo)", nn << 1);
            return;
        }
        if (top5 == 0b11101) {
            snprintf(out, outSize, "UNDEFINED");
            return;
        }

        snprintf(out, outSize, "??? (0x%04X)", instr);
    }
};