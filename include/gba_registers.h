// gba_registers.h
#pragma once

// ── Memory Map ──────────────────────────────────────────────────
#define MEM_BIOS        0x00000000
#define MEM_EWRAM       0x02000000
#define MEM_IWRAM       0x03000000
#define MEM_IO          0x04000000
#define MEM_PRAM        0x05000000
#define MEM_VRAM        0x06000000
#define MEM_OAM         0x07000000
#define MEM_ROM         0x08000000

// ── Display ─────────────────────────────────────────────────────
#define REG_DISPCNT     0x04000000  // LCD control
#define REG_DISPSTAT    0x004   // LCD status
#define REG_VCOUNT      0x006   // current scanline

// ── Interrupts ──────────────────────────────────────────────────
#define REG_IE          0x200   // interrupt enable
#define REG_IF          0x202   // interrupt flags (write 1 to clear)
#define REG_IME         0x208   // interrupt master enable

// ── DMA ─────────────────────────────────────────────────────────
#define DMA0_BASE       0x040000B0
#define DMA1_BASE       0x0BC
#define DMA2_BASE       0x0C8
#define DMA3_BASE       0x0D4
#define DMA_SAD         0x0     // source address offset
#define DMA_DAD         0x4     // destination address offset
#define DMA_CNT         0x8     // count + control offset

// ── Timers ──────────────────────────────────────────────────────
#define REG_TM0CNT_L    0x100   // timer 0 counter/reload
#define REG_TM0CNT_H    0x102   // timer 0 control
#define REG_TM1CNT_L    0x104
#define REG_TM1CNT_H    0x106
#define REG_TM2CNT_L    0x108
#define REG_TM2CNT_H    0x10A
#define REG_TM3CNT_L    0x10C
#define REG_TM3CNT_H    0x10E

// ── Input ───────────────────────────────────────────────────────
#define REG_KEYINPUT    0x130   // button state (0=pressed)
#define REG_KEYCNT      0x132   // key interrupt control

// ── Sound ───────────────────────────────────────────────────────
#define REG_SOUNDCNT_L  0x080
#define REG_SOUNDCNT_H  0x082
#define REG_SOUNDCNT_X  0x084
#define REG_FIFO_A      0x0A0
#define REG_FIFO_B      0x0A4

// ── IRQ bits ────────────────────────────────────────────────────
#define IRQ_VBLANK      (1 << 0)
#define IRQ_HBLANK      (1 << 1)
#define IRQ_VCOUNT      (1 << 2)
#define IRQ_TIMER0      (1 << 3)
#define IRQ_TIMER1      (1 << 4)
#define IRQ_TIMER2      (1 << 5)
#define IRQ_TIMER3      (1 << 6)
#define IRQ_DMA0        (1 << 8)
#define IRQ_DMA1        (1 << 9)
#define IRQ_DMA2        (1 << 10)
#define IRQ_DMA3        (1 << 11)
#define IRQ_KEYPAD      (1 << 12)