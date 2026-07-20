#pragma once
#include<cstdint>
#include<vector>
#include <core/bus.h>

struct Bus;

struct PPU {

	std::vector<uint32_t> frameBuffer;
	uint8_t bgPrioBuffer[240];
	// Layer buffers — filled each scanline, composited by composeScanLine
	uint16_t bgLineBuffer[4][240];   // 15-bit GBA color per BG, 0x8000 = transparent
	uint16_t objLineBuffer[240];     // 15-bit GBA color, 0x8000 = no sprite
	uint8_t  objPriorityBuffer[240]; // sprite priority at each pixel
	bool     objSemiTransBuffer[240];// true = semi-transparent sprite pixel
	int32_t bg2RefX = 0, bg2RefY = 0;
	int32_t bg3RefX = 0, bg3RefY = 0;
	Bus& bus;


	PPU(Bus& b);
	void composeScanLine(uint8_t scanline);
	void renderScanLine(uint8_t scanline);
	void renderMode0(uint8_t scanline);
	void renderMode1(uint8_t scanline);
	void renderMode3(uint8_t scanline);
	void renderMode4(uint8_t scanline);
	void renderMode5(uint8_t scanline);
	void renderMode2(uint8_t scanline);
	void renderSprites(uint8_t scanline);
};