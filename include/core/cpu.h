#pragma once
#include <cstdint>
#include <cmath>
#include "core/bus.h"

const uint8_t MODE_USER = 0b10000;
const uint8_t MODE_FIQ = 0b10001;
const uint8_t MODE_IRQ = 0b10010;
const uint8_t MODE_SUPERVISOR = 0b10011;
const uint8_t MODE_ABORT = 0b10111;
const uint8_t MODE_UNDEFINED = 0b11011;
const uint8_t MODE_SYSTEM = 0b11111;

struct CPU {
    uint32_t reg[16] = { 0 };
    uint32_t cpsr = 0xD3;
    Bus& bus;
    bool halted = false;

    uint32_t reg_svc[2] = { 0 };
    uint32_t reg_irq[2] = { 0 };
    uint32_t reg_abt[2] = { 0 };
    uint32_t reg_und[2] = { 0 };
    uint32_t reg_fiq[7] = { 0 };
    uint32_t reg_user[7] = { 0 };

    uint32_t spsr_svc = 0;
    uint32_t spsr_irq = 0;
    uint32_t spsr_abt = 0;
    uint32_t spsr_und = 0;
    uint32_t spsr_fiq = 0;

    CPU(Bus& b) : bus(b) {}
    void skipBIOS();
    int Step();
    int Execute(uint32_t instruction);
    int executeThumb(uint16_t instruction);
    void triggerFIQ();
    void triggerIRQ();
    void switchMode(uint8_t newMode);
    void restoreCPSRFromSPSR();
    void setCPSR(uint32_t val);
    void reset();
    uint32_t getReg(uint8_t index);


private:
    bool  const   checkCondition(uint32_t cond);
    void     setFlags(uint32_t result, uint64_t full, uint32_t Rn, uint32_t op2, bool isSub);
    uint32_t rotate_right(uint32_t value, uint8_t amount);
    uint32_t apply_shift(uint32_t value, uint8_t shift);
    void executeBranch(uint32_t instruction);
    void executeBx(uint32_t instruction);
    void executeDataProcessing(uint32_t instruction);
    void executeMultiply(uint32_t instruction);
    void executeMultiplyLong(uint32_t instruction);
    void executeLoadStore(uint32_t instruction);
    void executeHalfwordTransfer(uint32_t instruction);
    void executeSWP(uint32_t instruction);
    void executeLoadStoreMultiple(uint32_t instruction);
    void executePSRTransfer(uint32_t instruction);
    void executeSWI(uint32_t instruction);
    void thumbMoveShifted(uint16_t instruction);
    void thumbAddSubtract(uint16_t instruction);
    void  thumbMoveImmediate(uint16_t instruction);
    void  thumbDataProcessing(uint16_t instruction);
    void  thumbHiRegister(uint16_t instruction);
    void thumbPCRelativeLDR(uint16_t instruction);
    void  thumbLoadStoreRegister(uint16_t instruction);
    void thumbLoadStoreSign(uint16_t instruction);
    void thumbLoadStoreImmediate(uint16_t instruction);
    void thumbLoadStoreHalfword(uint16_t instruction);
    void thumbSPRelative(uint16_t instruction);
    void thumbLoadAddress(uint16_t instruction);
    void thumbAddSP(uint16_t instruction);
    void thumbPushPop(uint16_t instruction);
    void thumbMultipleLoadStore(uint16_t instruction);
    void thumbConditionalBranch(uint16_t instruction);
    void thumbUnconditionalBranch(uint16_t instruction);
    void thumbLongBranch(uint16_t instruction);
};