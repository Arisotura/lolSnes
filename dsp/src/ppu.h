#pragma once

typedef struct
{
	// 00 - general
	u16 DispCnt;
	u16 SetIni;
	
	u16 BGMode;
	u16 BGMosaic;
	u16 BGEnable;
	u16 M7Mode;
	
	u16 ColorMath;
	u16 SubBackdrop;
	
	u16 BGOld;
	u16 M7Old;
	
	u16 _pad0[12];
	
	// 10 - BG layers
	u16 BGScr[4];
	u16 BGChr[4];
	u16 BGXScr2[4];
	u16 BGYScr2[4];
	u16 BGXPos[4];
	u16 BGYPos[4];
	
	s16 M7XPos;
	s16 M7YPos;
	s16 M7XCenter;
	s16 M7YCenter;
	s16 M7A;
	s16 M7B;
	s16 M7C;
	s16 M7D;
	
	// 20 - OBJ
	u16 OBJChr;
	u16 OBJGap;
	u16 OBJWidth[2];
	u16 OBJHeight[2];
	
	// 30 - window
	u16 WindowDirty;
	u16 WindowX[4];
	u16 BGWindowMask[4];
	u16 OBJWindowMask;
	u16 ColorMathWindowMask;
	u16 BGWindowCombine[4];
	u16 OBJWindowCombine;
	u16 ColorMathWindowCombine;
	u16 WindowEnable;
	
	// 40 - CGRAM/VRAM/OAM access
	u16 CGRAMAddr;
	u16 CGRAMVal;
	u16 VRAMInc;
	u16 VRAMStep;
	u16 VRAMAddr;
	u16 VRAMPref;
	u16 OAMAddr;
	u16 OAMVal;
	u16 OAMReload;
	u16 OAMPrio;
	u16 FirstOBJ;
	u16 _pad4[10];
	
	// 50 - CGRAM
	u16 CGRAM[256];
	
	// 150 - OAM
	u16 OAM[256+16];
	
} PPU_State;

void PPU_Reset(void);
void PPU_Write8(u16 addr, u16 val);
void PPU_DrawScanline(u16 line);
void PPU_VBlank(void);

void PPU_DoDrawScanline(PPU_State* state, u16* out, u16 line);
void PPU_test(u16* out);
void PPU_test2(u16* out);
