/*
    Copyright 2013 Mega-Mario

    This file is part of lolSnes.

    lolSnes is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    lolSnes is distributed in the hope that it will be useful, but WITHOUT ANY 
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along 
    with lolSnes. If not, see http://www.gnu.org/licenses/.
*/

#include <nds.h>
#include <stdio.h>

#include "memory.h"

#include "ppu_prio.h"

// PPU
//
// TODO LIST:
// * (take SPC stuff out of the PPU?)


#define BG_CHR_BASE		0x06000000
#define BG_SCR_BASE		0x06040000
#define BG_M7_BASE		0x06048000
#define OBJ_BASE		0x06400000
#define BG_PAL_BASE		0x06890000


u32 PPU_Planar2Linear[16][16];


// TODO make this configurable
u16 PPU_YOffset = 16;

u16 PPU_VCount DTCM_DATA = 0;
bool PPU_MissedVBlank = false;

u8 PPU_CGRAMAddr = 0;
u16 PPU_CurColor = 0xFFFF;
u8 PPU_CGRFlag = 0;
u8 PPU_CGRDirty = 0;

u8 PPU_VRAM[0x10000];
u16 PPU_VRAMAddr = 0;
u16 PPU_VRAMVal = 0;
u8 PPU_VRAMInc = 0;
u8 PPU_VRAMStep = 0;
u16 PPU_VRAMPref = 0;

u8 PPU_OAM[0x220];
u16 PPU_OAMAddr = 0;
u16 PPU_OAMVal = 0;
u8 PPU_OAMPrio = 0;
u8 PPU_FirstOAM = 0;
u8 PPU_OAMDirty = 0;

u8 PPU_NumSprites DTCM_DATA = 0;

// VRAM mapping table
// each entry applies to 2K of SNES VRAM
typedef struct
{
	u16 ChrUsage;		// bit0-3 -> BG0-BG3, bit4 -> OBJ
	u16 ScrUsage;		// same
	
} PPU_VRAMBlock;
PPU_VRAMBlock PPU_VRAMMap[32];

// 1024 tiles -> 16K SNES / 64K DS
// 1 BG -> 2 to 8 scr blocks
// max scr: 32k

// mode 0: 	4/4/4/4		64k/64k/64k/64k		256k
// mode 1:	16/16/4		64k/64k/64k			192k
// mode 2: 	16/16		64k/64k				128k
// mode 3: 	256/16		64k/64k				128k
// mode 4:	256/4		64k/64k				128k
// mode 5:	16/4		64k/64k				128k
// mode 6:	16			64k					64k
// mode 7:	256			32k tileset+tilemap

// OBJ: max size = 64x64 -> 64 tiles
// max tiles: 512 -> 16k bytes
// (allocate more OBJ for some workarounds)

// VRAM ALLOCATION
// 0x06040000 : bank D: BG scr data, mode7 graphics
// 0x06000000 : bank A/B: BG chr data
// 0x06400000 : bank E: OBJ chr data
// bank F: BG ext palettes (slot 2-3)
// bank C: ARM7 VRAM
// bank H: bottom screen

// keeps track of blank tiles
// 1 bit per 2 pixels
//u32 PPU_TileUsage[4][1024];

typedef struct
{
	u16 ChrBase;
	u16 ScrBase;
	u32 ChrSize;
	u16 ScrSize;	// size of the scr data (2K/4K/8K)
	u16 MapSize;	// BG size (0-3)
	
	int ColorDepth;
	u16 MapPalOffset;
	u32 TilePalOffset;
	
	u16 ScrollX, ScrollY;
	
	u16 BGCnt;
	
	u8 DSBG;
	u8 Mask;
	
	u16* BGCNT;
	u16* BGHOFS;
	u16* BGVOFS;

} PPU_Background;
PPU_Background PPU_BG[4];

u8 _PPU_BGPrio[9][8] = 
{
	{1, 1, 3, 3, 0, 0, 2, 2},	// mode 0
	{1, 1, 3, 0, 0, 0, 2, 0},	// mode 1
	{2, 3, 0, 0, 0, 1, 0, 0},	// mode 2
	{2, 3, 0, 0, 0, 1, 0, 0},	// mode 3
	{2, 3, 0, 0, 0, 1, 0, 0},	// mode 4
	{2, 3, 0, 0, 0, 1, 0, 0},	// mode 5
	{2, 0, 0, 0, 0, 0, 0, 0},	// mode 6
	{2, 0, 0, 0, 0, 0, 0, 0},	// mode 7
	{2, 2, 3, 0, 1, 1, 0, 0}	// mode 1 w/ high prio
};
u8* PPU_BGPrio;

u8 PPU_CurPrioNum;
u8* PPU_CurPrio;

u16 PPU_MasterBright;
u8 PPU_Mode DTCM_DATA;
u8 PPU_ModeNow;
u8 PPU_LastNon7Mode;
u8 PPU_BG3Prio;

u8 PPU_BGMain, PPU_BGSub;
u8 PPU_BGMask, PPU_BGEnable;
u16 PPU_MainBackdrop, PPU_SubBackdrop;

u8 PPU_OBJSize;
u16 PPU_OBJBase;
u16 PPU_OBJGap;

/*
         Val Small  Large
         0 = 8x8    16x16    ;Caution:
         1 = 8x8    32x32    ;In 224-lines mode, OBJs with 64-pixel height
         2 = 8x8    64x64    ;may wrap from lower to upper screen border.
         3 = 16x16  32x32    ;In 239-lines mode, the same problem applies
         4 = 16x16  64x64    ;also for OBJs with 32-pixel height.
         5 = 32x32  64x64
         6 = 16x32  32x64 (undocumented)
         7 = 16x32  32x32 (undocumented)
*/
u8 _PPU_OBJSizes[16] = 
{
	0, 1,
	0, 2,
	0, 3,
	1, 2,
	1, 3,
	2, 3,
	0x82, 0x83,
	0x82, 2
};
u8* PPU_OBJSizes;

u16 PPU_OBJList[4*128];
u8 PPU_OBJPrioList[128] DTCM_DATA;

u8 PPU_SpriteSize[16] =
{
	8, 16, 32, 64,
	8, 8, 16, 32,
	8, 16, 32, 64,
	16, 32, 32, 64
};

u16 PPU_OBJPrio[4] DTCM_DATA;


u8 PPU_M7Old;

s16 PPU_MulA;
s8 PPU_MulB;
s32 PPU_MulResult;

int PPU_M7RefDirty DTCM_DATA = 0;
s16 PPU_M7RefX DTCM_DATA, 
	PPU_M7RefY DTCM_DATA;
s16 PPU_M7ScrollX DTCM_DATA, 
	PPU_M7ScrollY DTCM_DATA;
s16 PPU_M7A DTCM_DATA, 
	PPU_M7B DTCM_DATA, 
	PPU_M7C DTCM_DATA, 
	PPU_M7D DTCM_DATA;


u32 Mem_WRAMAddr;


// per-scanline change support

#define MAX_LINE_CHANGES 20

typedef struct
{
	void (*Action)(u32);
	u32 Data;
	
} PPU_LineChange;
PPU_LineChange PPU_LineChanges[193][MAX_LINE_CHANGES];
u8 PPU_NumLineChanges[193] DTCM_DATA;



void PPU_Reset()
{
	int i;
	
	for (i = 0; i < 256; i++)
	{
		int b1 = i & 0x0F, b2 = i >> 4;
		
		PPU_Planar2Linear[b1][b2]
				= ((b1 & 0x08) >> 3) | ((b2 & 0x08) >> 2)
				| ((b1 & 0x04) << 6) | ((b2 & 0x04) << 7)
				| ((b1 & 0x02) << 15) | ((b2 & 0x02) << 16)
				| ((b1 & 0x01) << 24) | ((b2 & 0x01) << 25);
	}
	
	PPU_M7RefDirty = 0;
	PPU_M7RefX = 0;
	PPU_M7RefY = 0;
	
	PPU_VCount = 0;
	PPU_MissedVBlank = false;
	
	PPU_CGRAMAddr = 0;
	PPU_CurColor = 0xFFFF;
	PPU_CGRFlag = 0;
	PPU_CGRDirty = 0;
	
	for (i = 0; i < 0x400; i += 4)
		*(u32*)(0x05000000 + i) = 0;
	for (i = 0; i < 0x400; i += 4)
		*(u32*)(0x07000000 + i) = 0;
	
	PPU_VRAMAddr = 0;
	PPU_VRAMVal = 0;
	PPU_VRAMPref = 0;
	
	PPU_OAMAddr = 0;
	PPU_OAMVal = 0;
	PPU_OAMPrio = 0;
	PPU_FirstOAM = 0;
	PPU_OAMDirty = 0;
	
	PPU_NumSprites = 0;
	
	for (i = 0; i < 32; i++)
	{
		PPU_VRAMMap[i].ChrUsage = 0;
		PPU_VRAMMap[i].ScrUsage = 0;
	}
	/*for (i = 0; i < 4; i++)
		PPU_VRAMMap[i].ChrUsage = 0x1F;
	for (i = 8; i < 8; i++)
		PPU_VRAMMap[i].ChrUsage = 0x0F;
	PPU_VRAMMap[0].ScrUsage = 0x1F;*/
	
	/*for (i = 0; i < 1024; i++)
	{
		PPU_TileUsage[0][i] = 0;
		PPU_TileUsage[1][i] = 0;
		PPU_TileUsage[2][i] = 0;
		PPU_TileUsage[3][i] = 0;
	}*/
	
	for (i = 0; i < 0x10000; i += 4)
		*(u32*)&PPU_VRAM[i] = 0;
		
	for (i = 0; i < 4; i++)
	{
		PPU_Background* bg = &PPU_BG[i];
		bg->ChrBase = 0;
		bg->ChrSize = 0;
		bg->ScrBase = 0;
		bg->ColorDepth = 0;
		
		bg->ScrollX = 0;
		bg->ScrollY = 0;
	}
	
	
	for (i = 0; i < 193; i++)
	{
		PPU_LineChanges[i][0].Action = 0;
		PPU_NumLineChanges[i] = 0;
	}
	
	
	PPU_MasterBright = 0;
	PPU_Mode = -1;
	PPU_LastNon7Mode = -2;
	PPU_BG3Prio = 0;
	
	PPU_BGMain = 0;
	PPU_BGSub = 0;
	PPU_MainBackdrop = 0;
	PPU_SubBackdrop = 0;
	
	
	PPU_OBJSize = 0;
	PPU_OBJBase = 0;
	PPU_OBJGap = 0;
	PPU_OBJSizes = _PPU_OBJSizes;
	
	for (i = 0; i < 4*128; i++)
		PPU_OBJList[i] = 0;
		
	PPU_OBJPrio[0] = 3 << 10;
	PPU_OBJPrio[1] = 2 << 10;
	PPU_OBJPrio[2] = 1 << 10;
	PPU_OBJPrio[3] = 0 << 10;
		
	PPU_M7Old = 0;
	PPU_MulA = 0;
	PPU_MulB = 0;
	PPU_MulResult = 0;
		
		
	Mem_WRAMAddr = 0;
		
	
	// allocate VRAM
	*(u8*)0x04000240 = 0x81;
	*(u8*)0x04000241 = 0x89;
	*(u8*)0x04000243 = 0x91;
	*(u8*)0x04000244 = 0x82;
	*(u8*)0x04000245 = 0x8C;
	
	// setup BGs
	*(u32*)0x04000000 = 0x40010000 | (4 << 27);
	*(u16*)0x04000008 = PPU_BG[0].BGCnt = 0x2080 | (0 << 4) | (0 << 10);
	*(u16*)0x0400000A = PPU_BG[1].BGCnt = 0x2081 | (1 << 4) | (1 << 10);
	*(u16*)0x0400000C = PPU_BG[2].BGCnt = 0x2082 | (2 << 4) | (2 << 10);
	*(u16*)0x0400000E = PPU_BG[3].BGCnt = 0x2083 | (3 << 4) | (3 << 10);
	
	
	PPU_ModeNow = 0;
	PPU_ModeChange(0);
	
	
	printf("vram = %08X | %08X\n", (u32)&PPU_VRAM, (u32)&PPU_VRAMMap);
}


inline void PPU_UpdateOBJPrio()
{
	u16* oam = (u16*)0x07000002;
	u8* priolist = &PPU_OBJPrioList[0];
	u8 i;
	
	for (i = 0; i < PPU_NumSprites; i++)
	{
		u16 attr = *oam & 0xF3FF;
		u16 prio = PPU_OBJPrio[*priolist++];
		*oam = attr | prio;
		
		oam += 4;
	}
}


void PPU_WriteReg16(u32 data);


void PPU_ScheduleLineChange(void (*action)(u32), u32 data)
{
	s16 vcount = PPU_VCount + 1 - PPU_YOffset;
	u16 ds_vcount = *(vu16*)0x04000006;
	
	// if the change occured during the vblank, schedule it for the DS vblank
	// if it occured in a hidden scanline, apply it now
	if (vcount > 191) vcount = 192;
	else if (vcount < 0)
	{
		(*action)(data);
		return;
	}
	
	// if the change occured too late, apply it now, and 
	// schedule it for the next frame to make up for the lag
	if ((ds_vcount < 192 && ds_vcount >= vcount) || PPU_MissedVBlank)
		(*action)(data);
	
	// if the last change was the same thing, just overwrite its data with the new one
	// TODO make this less ugly
	u8 nchanges = PPU_NumLineChanges[vcount];
	PPU_LineChange* change;
	if (nchanges > 0)
	{
		change = &PPU_LineChanges[vcount][nchanges - 1];
		if (change->Action == action && action != PPU_WriteReg16)
		{
			change->Data = data;
			return;
		}
		
		change++;
	}
	else
		change = &PPU_LineChanges[vcount][nchanges];
	
	change->Action = action;
	change->Data = data;
	if (nchanges < (MAX_LINE_CHANGES-1))
		PPU_NumLineChanges[vcount]++;
}

void PPU_ResetLineChanges()
{
	int i;
	
	for (i = 0; i < 192; i += 4)
		*(u32*)&PPU_NumLineChanges[i] = 0;
	PPU_NumLineChanges[192] = 0;
}

inline void PPU_DoLineChanges(u32 line)
{
	int i;
	register u8 limit = PPU_NumLineChanges[line];
	register PPU_LineChange* change = &PPU_LineChanges[line][0];
	
	for (i = 0; i < limit; i++)
	{
		(*(change->Action))(change->Data);
		change++;
	}
}


ITCM_CODE void PPU_WriteReg16(u32 data)
{
	u16 val = data & 0xFFFF;
	u32 reg = 0x04000000 | (data >> 16);
	
	*(vu16*)reg = val;
	if ((reg & 0x3FC) == 0x10) PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_HandleModeChange(u32 val)
{
	if (val & 0xF0) iprintf("!! 16x16 TILES NOT SUPPORTED\n");
	
	PPU_BG3Prio = ((val & 0x0F) == 0x09);
	if (PPU_BG3Prio)
	{
		PPU_OBJPrio[0] = 3 << 10;
		PPU_OBJPrio[1] = 3 << 10;
		PPU_OBJPrio[2] = 2 << 10;
		PPU_OBJPrio[3] = 1 << 10;
		PPU_UpdateOBJPrio();
	}
	else
	{
		PPU_OBJPrio[0] = 3 << 10;
		PPU_OBJPrio[1] = 2 << 10;
		PPU_OBJPrio[2] = 1 << 10;
		PPU_OBJPrio[3] = 0 << 10;
		PPU_UpdateOBJPrio();
	}
	
	u8 mode = val & 0x0F;
	if (mode == 9) mode = 8;
	else mode &= 0x07;
	PPU_CurPrio = &PPU_PrioTable[(mode << 9) + (PPU_CurPrioNum << 4)];
	
	PPU_ModeChange(val & 0x07);
}

inline void PPU_UpdateEnabledBGs()
{
	PPU_BGEnable = PPU_BGMain | PPU_BGSub;
	
	u32 dispcnt = *(vu32*)0x04000000;
	dispcnt &= 0xFFFFE0FF;
	
	if (PPU_Mode == 7)
	{
		if (PPU_BGEnable & 0x01) dispcnt |= 0x0400;
		// TODO EXTBG
	}
	else
	{
		if (PPU_BGEnable & 0x01) dispcnt |= (PPU_BG[0].Mask << 8);
		if (PPU_BGEnable & 0x02) dispcnt |= (PPU_BG[1].Mask << 8);
		if (PPU_BGEnable & 0x04) dispcnt |= (PPU_BG[2].Mask << 8);
		if (PPU_BGEnable & 0x08) dispcnt |= (PPU_BG[3].Mask << 8);
	}
	if (PPU_BGEnable & 0x10) dispcnt |= 0x1000;
	
	*(vu32*)0x04000000 = dispcnt;
	
	u8 prionum = PPU_BGSub & (PPU_BGMain ^ 0x1F);
	if (prionum != PPU_CurPrioNum)
	{
		PPU_CurPrioNum = prionum;
		// do shit
	}
}

ITCM_CODE void PPU_SetMainScreen(u32 val)
{
	PPU_BGMain = val;
	PPU_UpdateEnabledBGs();
}

ITCM_CODE void PPU_SetSubScreen(u32 val)
{
	PPU_BGSub = val;
	PPU_UpdateEnabledBGs();
}

ITCM_CODE void PPU_SetM7A(u32 val)
{
	PPU_M7A = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7B(u32 val)
{
	PPU_M7B = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7C(u32 val)
{
	PPU_M7C = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7D(u32 val)
{
	PPU_M7D = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7RefX(u32 val)
{
	if (val & 0x1000) val |= 0xE000;
	PPU_M7RefX = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7RefY(u32 val)
{
	if (val & 0x1000) val |= 0xE000;
	PPU_M7RefY = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7ScrollX(u32 val)
{
	if (val & 0x1000) val |= 0xE000;
	PPU_M7ScrollX = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7ScrollY(u32 val)
{
	if (val & 0x1000) val |= 0xE000;
	PPU_M7ScrollY = val;
	PPU_M7RefDirty = 1;
}


ITCM_CODE void PPU_UploadBGPal()
{
	u32* src = (u32*)0x05000000;
	u32* dst = (u32*)BG_PAL_BASE;
	u32* dst2 = dst + 0x800;
	int i;
	
	// Mode 7 not handled here (uses regular palette)
	
	switch (PPU_LastNon7Mode)
	{
		case 0:  // 0-7: 4 colors
			for (i = 0; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				dst += (252 >> 1);
				dst2 += (252 >> 1);
			}
			dst -= (2032 >> 1);
			dst2 -= (2032 >> 1);
			for (i = 0; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				dst += (252 >> 1);
				dst2 += (252 >> 1);
			}
			dst -= (2032 >> 1);
			dst2 -= (2032 >> 1);
			for (i = 0; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				dst += (252 >> 1);
				dst2 += (252 >> 1);
			}
			dst -= (2032 >> 1);
			dst2 -= (2032 >> 1);
			for (i = 0; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				dst += (252 >> 1);
				dst2 += (252 >> 1);
			}
			break;
			
		case 1:  // 0-7: 16 colors, 8-15: 4 colors
		case 5:
			for (i = 0; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				
				dst += (240 >> 1);
				dst2 += (240 >> 1);
			}
			src -= (128 >> 1);
			for (i = 0; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				dst += (252 >> 1);
				dst2 += (252 >> 1);
			}
			break;
			
		case 2:  // 0-7: 16 colors
		case 6:
			for (i = 0; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				
				dst += (240 >> 1);
				dst2 += (240 >> 1);
			}
			break;
			
		case 3:  // 0: 256 colors, 0-7: 16 colors
			for (i = 0; i < 128; i++)
			{
				*dst++ = *dst2++ = *src++;
			}
			src -= (240 >> 1);
			for (i = 1; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				
				dst += (240 >> 1);
				dst2 += (240 >> 1);
			}
			break;
			
		case 4:  // 4: 256 colors, 0-7: 4 colors
			for (i = 0; i < 128; i++)
			{
				*dst++ = *dst2++ = *src++;
			}
			src -= (252 >> 1);
			for (i = 1; i < 8; i++)
			{
				*dst++ = *dst2++ = *src++;
				*dst++ = *dst2++ = *src++;
				
				dst += (240 >> 1);
				dst2 += (240 >> 1);
			}
			break;
	}
}

void PPU_UploadBGChr(int nbg)
{
	PPU_Background* bg = &PPU_BG[nbg];
	u16 chrbase = bg->ChrBase;
	
	if (bg->ColorDepth == 0)
		return;
	
	if (bg->ColorDepth == 4)
	{
		u8* bp12 = &PPU_VRAM[chrbase];
		u32* dst = (u32*)(BG_CHR_BASE + (nbg << 16));
		
		int t;
		for (t = 0; t < 1024; t++)
		{
			int y;
			//u32 usage = 0;
			for (y = 0; y < 8; y++)
			{
				u8 	b1 = *bp12++,
					b2 = *bp12++;

				*dst++ = PPU_Planar2Linear[b1 >> 4][b2 >> 4];
				*dst++ = PPU_Planar2Linear[b1 & 0xF][b2 & 0xF];
						
				/*if (*(dst-4)) usage |= 0x8;
				if (*(dst-3)) usage |= 0x4;
				if (*(dst-2)) usage |= 0x2;
				if (*(dst-1)) usage |= 0x1;
				usage <<= 4;*/
			}
			
			//PPU_TileUsage[nbg][t] = usage;
		}
	}
	else if (bg->ColorDepth == 16)
	{
		u8* bp12 = &PPU_VRAM[chrbase];
		u8* bp34 = bp12 + 16;
		u32* dst = (u32*)(BG_CHR_BASE + (nbg << 16));
		
		int t;
		for (t = 0; t < 1024; t++)
		{
			int y;
			//u32 usage = 0;
			for (y = 0; y < 8; y++)
			{
				u8 	b1 = *bp12++,
					b2 = *bp12++,
					b3 = *bp34++,
					b4 = *bp34++;

				*dst++ = PPU_Planar2Linear[b1 >> 4][b2 >> 4] | (PPU_Planar2Linear[b3 >> 4][b4 >> 4] << 2);
				*dst++ = PPU_Planar2Linear[b1 & 0xF][b2 & 0xF] | (PPU_Planar2Linear[b3 & 0xF][b4 & 0xF] << 2);
						
				/*if (*(dst-4)) usage |= 0x8;
				if (*(dst-3)) usage |= 0x4;
				if (*(dst-2)) usage |= 0x2;
				if (*(dst-1)) usage |= 0x1;
				usage <<= 4;*/
			}
			
			//PPU_TileUsage[nbg][t] = usage;
			bp12 += 16;
			bp34 += 16;
		}
	}
	else if (bg->ColorDepth == 256)
	{
		u8* bp12 = &PPU_VRAM[chrbase];
		u8* bp34 = bp12 + 16;
		u8* bp56 = bp34 + 16;
		u8* bp78 = bp56 + 16;
		u32* dst = (u32*)(BG_CHR_BASE + (nbg << 16));
		
		int t;
		for (t = 0; t < 1024; t++)
		{
			int y;
			for (y = 0; y < 8; y++)
			{
				u8 	b1 = *bp12++,
					b2 = *bp12++,
					b3 = *bp34++,
					b4 = *bp34++,
					b5 = *bp56++,
					b6 = *bp56++,
					b7 = *bp78++,
					b8 = *bp78++;

				*dst++ 	= PPU_Planar2Linear[b1 >> 4][b2 >> 4] | (PPU_Planar2Linear[b3 >> 4][b4 >> 4] << 2)
						| (PPU_Planar2Linear[b5 >> 4][b6 >> 4] << 4) | (PPU_Planar2Linear[b7 >> 4][b8 >> 4] << 6);
				*dst++ 	= PPU_Planar2Linear[b1 & 0xF][b2 & 0xF] | (PPU_Planar2Linear[b3 & 0xF][b4 & 0xF] << 2)
						| (PPU_Planar2Linear[b5 & 0xF][b6 & 0xF] << 4) | (PPU_Planar2Linear[b7 & 0xF][b8 & 0xF] << 6);
			}
			
			bp12 += 48;
			bp34 += 48;
			bp56 += 48;
			bp78 += 48;
		}
	}
}

void PPU_UploadBGScr(int nbg)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (bg->ColorDepth == 0)
		return;
	
	int bgsize = bg->ScrSize;
	u16* src = (u16*)&PPU_VRAM[bg->ScrBase];
	u16* dst = (u16*)(BG_SCR_BASE + (nbg << 13));
	register u16 palbase = bg->MapPalOffset;
	
	int t;
	for (t = 0; t < bgsize; t++)
	{
		u16 stile = *src++;
		u16 dtile = stile & 0x03FF;
		
		if (stile & 0x2000)
		{
			// tile with priority
			// TODO
		}
		
		if (stile & 0x4000)
			dtile |= 0x0400;
		if (stile & 0x8000)
			dtile |= 0x0800;
		
		if (palbase != 0xFF)
			dtile |= ((stile & 0x1C00) << 2) | palbase;
			
		*dst++ = dtile;
	}
}

void PPU_UploadOBJChr()
{
	u16 chrbase = PPU_OBJBase;
	
	u8* bp12 = &PPU_VRAM[chrbase];
	u8* bp34 = bp12 + 16;
	u16* dst = (u16*)OBJ_BASE;
	
	int t;
	for (t = 0; t < 512; t++)
	{
		int y;
		for (y = 0; y < 8; y++)
		{
			u8 	b1 = *bp12++,
				b2 = *bp12++,
				b3 = *bp34++,
				b4 = *bp34++;

			*dst++ 	= ((b1 & 0x80) >> 7) | ((b2 & 0x80) >> 6) | ((b3 & 0x80) >> 5) | ((b4 & 0x80) >> 4)
					| ((b1 & 0x40) >> 2) | ((b2 & 0x40) >> 1) | ((b3 & 0x40)) | ((b4 & 0x40) << 1)
					| ((b1 & 0x20) << 3) | ((b2 & 0x20) << 4) | ((b3 & 0x20) << 5) | ((b4 & 0x20) << 6)
					| ((b1 & 0x10) << 8) | ((b2 & 0x10) << 9) | ((b3 & 0x10) << 10) | ((b4 & 0x10) << 11);
			*dst++ 	= ((b1 & 0x08) >> 3) | ((b2 & 0x08) >> 2) | ((b3 & 0x08) >> 1) | (b4 & 0x08)
					| ((b1 & 0x04) << 2) | ((b2 & 0x04) << 3) | ((b3 & 0x04) << 4) | ((b4 & 0x04) << 5)
					| ((b1 & 0x02) << 7) | ((b2 & 0x02) << 8) | ((b3 & 0x02) << 9) | ((b4 & 0x02) << 10)
					| ((b1 & 0x01) << 12) | ((b2 & 0x01) << 13) | ((b3 & 0x01) << 14) | ((b4 & 0x01) << 15);
		}
		
		bp12 += 16;
		bp34 += 16;
		
		if ((t & 0xF) == 0xF)
			dst += 256;
	}
}

void PPU_SetupBG(int nbg, int depth, u32 tilepaloffset, u16 mappaloffset)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	register u8 dsbg = PPU_CurPrio[nbg];
	iprintf("BG%d ON DS BG%d\n", nbg, dsbg);
	bg->DSBG = dsbg;
	bg->Mask = 1 << dsbg;
	
	bg->BGCNT = (u16*)(0x04000008 + (dsbg << 1));
	bg->BGHOFS = (u16*)(0x04000010 + (dsbg << 2));
	bg->BGVOFS = (u16*)(0x04000012 + (dsbg << 2));
	
	if (bg->ColorDepth != depth)
	{
		int olddepth = bg->ColorDepth;
		bg->ColorDepth = depth;
		
		register int bmask = (1 << nbg), nbmask = ~bmask;
		int i, size;
		
		int oldsize = bg->ChrSize >> 11;
		switch (depth)
		{
			case 0: bg->ChrSize = 0; break;
			case 4: bg->ChrSize = 0x4000; break;
			case 16: bg->ChrSize = 0x8000; break;
			case 256: bg->ChrSize = 0x10000; break;
		}
		
		size = bg->ChrSize >> 11;
		if (size != oldsize)
		{
			for (i = 0; i < oldsize; i++)
				if (((bg->ChrBase >> 11) + i) < 32)
					PPU_VRAMMap[(bg->ChrBase >> 11) + i].ChrUsage &= nbmask;
			for (i = 0; i < size; i++)
				if (((bg->ChrBase >> 11) + i) < 32)
					PPU_VRAMMap[(bg->ChrBase >> 11) + i].ChrUsage |= bmask;
		}
		
		bg->TilePalOffset = tilepaloffset;
		
		if (depth != 0)
		{
			PPU_UploadBGChr(nbg);
			if (olddepth == 0 || bg->MapPalOffset != mappaloffset) 
			{
				bg->MapPalOffset = mappaloffset;
				PPU_UploadBGScr(nbg);
			}
		}
		else
			return;
	}
	else
	{
		if (mappaloffset != bg->MapPalOffset)
		{
			bg->MapPalOffset = mappaloffset;
			PPU_UploadBGScr(nbg);
		}
		if (tilepaloffset != bg->TilePalOffset)
		{
			bg->TilePalOffset = tilepaloffset;
			PPU_UploadBGChr(nbg);
		}
	}
	
	*bg->BGCNT = bg->BGCnt;
}

void PPU_ModeChange(u8 newmode)
{
	int i;
	
	if (newmode == PPU_Mode) return;
	
	PPU_BGPrio = &_PPU_BGPrio[PPU_BG3Prio ? 8 : newmode][0];
	
	if ((PPU_Mode == 7) ^ (newmode == 7))
	{
		PPU_Mode = newmode;
		u8 bgenable = (PPU_BGMain | PPU_BGSub);
		
		if (newmode == 7)
		{
			// switch to mode 7
			*(u32*)0x04000000 = 0x00010002 | (4 << 24) | (4 << 27) | ((bgenable & 0x01) ? 0x400:0) | ((bgenable & 0x10) ? 0x1000:0);
			*(u16*)0x0400000C = 0xC082 | (2 << 2) | (24 << 8);
			//*(u16*)0x0400000E = 0x0083 | (3 << 4) | (3 << 10);
			
			return;
		}
		
		// switch from mode 7
		*(u32*)0x04000000 = 0x40010000 | (4 << 27) | (bgenable << 8);

		if (PPU_LastNon7Mode == newmode)
		{
			/**(u16*)0x04000008 = PPU_BG[0].BGCnt;
			*(u16*)0x0400000A = PPU_BG[1].BGCnt;
			*(u16*)0x0400000C = PPU_BG[2].BGCnt;
			*(u16*)0x0400000E = PPU_BG[3].BGCnt;*/
			return;
		}
	}
	
	PPU_BG[0].BGCnt = (PPU_BG[0].BGCnt & 0xFFFFFFFC) | PPU_BGPrio[0];
	PPU_BG[1].BGCnt = (PPU_BG[1].BGCnt & 0xFFFFFFFC) | PPU_BGPrio[1];
	PPU_BG[2].BGCnt = (PPU_BG[2].BGCnt & 0xFFFFFFFC) | PPU_BGPrio[2];
	PPU_BG[3].BGCnt = (PPU_BG[3].BGCnt & 0xFFFFFFFC) | PPU_BGPrio[3];
	/**(u16*)0x04000008 = PPU_BG[0].BGCnt;
	*(u16*)0x0400000A = PPU_BG[1].BGCnt;
	*(u16*)0x0400000C = PPU_BG[2].BGCnt;
	*(u16*)0x0400000E = PPU_BG[3].BGCnt;*/
	
	PPU_Mode = newmode;
	PPU_LastNon7Mode = newmode;
	
	switch (newmode)
	{
		case 0:
			PPU_SetupBG(0, 4, 0x00000000, 0);
			PPU_SetupBG(1, 4, 0x10101010, 0);
			PPU_SetupBG(2, 4, 0x20202020, 0);
			PPU_SetupBG(3, 4, 0x30303030, 0);
			break;
		
		case 1:
			PPU_SetupBG(0, 16, 0, 0);
			PPU_SetupBG(1, 16, 0, 0);
			PPU_SetupBG(2, 4, 0, 0x8000);
			PPU_SetupBG(3, 0, 0, 0);
			break;
			
		case 2:
			PPU_SetupBG(0, 16, 0, 0);
			PPU_SetupBG(1, 16, 0, 0);
			PPU_SetupBG(2, 0, 0, 0);	// TODO OFFSET PER TILE
			PPU_SetupBG(3, 0, 0, 0);
			break;
			
		case 3:
			PPU_SetupBG(0, 256, 0, 0xFF);
			PPU_SetupBG(1, 16, 0, 0);
			PPU_SetupBG(2, 0, 0, 0);
			PPU_SetupBG(3, 0, 0, 0);
			break;
			
		case 4:
			PPU_SetupBG(0, 256, 0, 0xFF);
			PPU_SetupBG(1, 4, 0, 0);
			PPU_SetupBG(2, 0, 0, 0);  // TODO OFFSET PER TILE
			PPU_SetupBG(3, 0, 0, 0);
			break;
			
		// TODO modes 5/6 (hires)
		
		default:
			iprintf("unsupported PPU mode %d\n", newmode);
			break;
	}
	
	PPU_CGRDirty = 1;
}


inline void PPU_SetXScroll(int nbg, u8 val)
{
	if (nbg == 0)
	{
		PPU_ScheduleLineChange(PPU_SetM7ScrollX, (val << 8) | PPU_M7Old);
		PPU_M7Old = val;
	}
	
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (!(bg->ScrollX & 0x8000))
		bg->ScrollX = val | 0x8000;
	else
	{
		bg->ScrollX &= 0xFF;
		bg->ScrollX |= ((val & 0x1F) << 8);
		if (PPU_ModeNow != 7)
			PPU_ScheduleLineChange(PPU_WriteReg16, 0x100000 | (bg->DSBG<<18) | bg->ScrollX);
	}
}

inline void PPU_SetYScroll(int nbg, u8 val)
{
	if (nbg == 0)
	{
		PPU_ScheduleLineChange(PPU_SetM7ScrollY, (val << 8) | PPU_M7Old);
		PPU_M7Old = val;
	}
	
	PPU_Background* bg = &PPU_BG[nbg];
	
	if (!(bg->ScrollY & 0x8000))
		bg->ScrollY = val | 0x8000;
	else
	{
		bg->ScrollY &= 0xFF;
		bg->ScrollY |= ((val & 0x1F) << 8);
		if (PPU_ModeNow != 7)
			PPU_ScheduleLineChange(PPU_WriteReg16, 0x120000 | (bg->DSBG<<18) | (bg->ScrollY + PPU_YOffset));
	}
}

inline void PPU_SetBGSCR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	int oldscrbase = bg->ScrBase;
	int oldsize = bg->ScrSize >> 11;
	
	bg->ScrBase = (val & 0xFC) << 9;
	bg->MapSize = val & 0x03;
	switch (val & 0x03)
	{
		case 0x00: bg->ScrSize = 2048; break;
		case 0x01:
		case 0x02: bg->ScrSize = 4096; break;
		case 0x03: bg->ScrSize = 8192; break;
	}
	
	if (bg->ScrBase != oldscrbase || (bg->ScrSize >> 11) != oldsize)
	{
		register int bmask = (1 << nbg), nbmask = ~bmask;
		
		int i, size = bg->ScrSize >> 11;
		for (i = 0; i < oldsize; i++)
			if (((oldscrbase >> 11) + i) < 32)
				PPU_VRAMMap[(oldscrbase >> 11) + i].ScrUsage &= nbmask;
		for (i = 0; i < size; i++)
			if (((bg->ScrBase >> 11) + i) < 32)
				PPU_VRAMMap[(bg->ScrBase >> 11) + i].ScrUsage |= bmask;
		
		PPU_UploadBGScr(nbg);
	}
	
	bg->BGCnt &= 0xFFFF3FFF;
	bg->BGCnt |= ((val & 0x03) << 14);
	
	if (PPU_ModeNow != 7)
		*bg->BGCNT = bg->BGCnt;
}

inline void PPU_SetBGCHR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	int oldchrbase = bg->ChrBase;
	bg->ChrBase = val << 13;
	
	if (bg->ChrBase != oldchrbase)
	{
		register int bmask = (1 << nbg), nbmask = ~bmask;
		
		int i, size = bg->ChrSize >> 11;
		for (i = 0; i < size; i++)
		{
			register int oldi = (oldchrbase >> 11) + i;
			if (oldi < 32) PPU_VRAMMap[oldi].ChrUsage &= nbmask;
		}
		for (i = 0; i < size; i++)
		{
			register int newi = (bg->ChrBase >> 11) + i;
			if (newi < 32) PPU_VRAMMap[newi].ChrUsage |= bmask;
		}
		
		PPU_UploadBGChr(nbg);
	}
}

inline void PPU_SetOBJCHR(u16 base, u16 gap)
{
	if (base != PPU_OBJBase || gap != PPU_OBJGap)
	{
		int i;
		for (i = 0; i < 0x8; i++)
		{
			register int oldi = (PPU_OBJBase >> 11) + i;
			if (i >= 0x4)
				oldi += (PPU_OBJGap >> 11);
			
			if (oldi < 32) PPU_VRAMMap[oldi].ChrUsage &= 0xFFEF;
		}
		for (i = 0; i < 0x8; i++)
		{
			register int newi = (base >> 11) + i;
			if (i >= 0x4)
				newi += (gap >> 11);
			
			if (newi < 32) PPU_VRAMMap[newi].ChrUsage |= 0x0010;
		}
		
		PPU_OBJBase = base;
		PPU_OBJGap = gap;
		
		PPU_UploadOBJChr();
	}
}

void PPU_UpdateVRAM_CHR(int nbg, u32 addr, u16 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	addr -= bg->ChrBase;
	u32 vramptr = BG_CHR_BASE + (nbg << 16);
	
	if (bg->ColorDepth == 4)
	{
		vramptr += (addr & 0xFFFFFFF0) << 2;
		vramptr += (addr & 0xE) << 2;
		
		*(u32*)vramptr = PPU_Planar2Linear[(val & 0x00F0) >> 4][val >> 12];
		*(u32*)(vramptr + 4) = PPU_Planar2Linear[val & 0x000F][(val & 0x0F00) >> 8];
								
		/*u32 usage = PPU_TileUsage[nbg][addr >> 4];
		int shift = (addr & 0xE) << 1;
		if (*(u16*)vramptr) usage |= 0x8;
		if (*(u16*)(vramptr + 2)) usage |= 0x4;
		if (*(u16*)(vramptr + 4)) usage |= 0x2;
		if (*(u16*)(vramptr + 6)) usage |= 0x1;
		PPU_TileUsage[nbg][addr >> 4] = (PPU_TileUsage*/
	}
	else if (bg->ColorDepth == 16)
	{
		vramptr += (addr & 0xFFFFFFE0) << 1;
		vramptr += (addr & 0xE) << 2;
		
		if (addr & 0x10)
		{
			*(u32*)vramptr = (*(u32*)vramptr & 0xF3F3F3F3) | (PPU_Planar2Linear[(val & 0x00F0) >> 4][val >> 12] << 2);
			*(u32*)(vramptr + 4) = (*(u32*)(vramptr + 4) & 0xF3F3F3F3) | (PPU_Planar2Linear[val & 0x000F][(val & 0x0F00) >> 8] << 2);
		}
		else
		{
			*(u32*)vramptr = (*(u32*)vramptr & 0xFCFCFCFC) | PPU_Planar2Linear[(val & 0x00F0) >> 4][val >> 12];
			*(u32*)(vramptr + 4) = (*(u32*)(vramptr + 4) & 0xFCFCFCFC) | PPU_Planar2Linear[val & 0x000F][(val & 0x0F00) >> 8];
		}
	}
	else if (bg->ColorDepth == 256)
	{
		vramptr += addr & 0xFFFFFFC0;
		vramptr += (addr & 0xE) << 2;
		
		switch (addr & 0x30)
		{
			case 0x00:
				*(u32*)vramptr = (*(u32*)vramptr & 0xFCFCFCFC) | PPU_Planar2Linear[(val & 0x00F0) >> 4][val >> 12];
				*(u32*)(vramptr + 4) = (*(u32*)(vramptr + 4) & 0xFCFCFCFC) | PPU_Planar2Linear[val & 0x000F][(val & 0x0F00) >> 8];
				break;
			
			case 0x10:
				*(u32*)vramptr = (*(u32*)vramptr & 0xF3F3F3F3) | (PPU_Planar2Linear[(val & 0x00F0) >> 4][val >> 12] << 2);
				*(u32*)(vramptr + 4) = (*(u32*)(vramptr + 4) & 0xF3F3F3F3) | (PPU_Planar2Linear[val & 0x000F][(val & 0x0F00) >> 8] << 2);
				break;
				
			case 0x20:
				*(u32*)vramptr = (*(u32*)vramptr & 0xCFCFCFCF) | (PPU_Planar2Linear[(val & 0x00F0) >> 4][val >> 12] << 4);
				*(u32*)(vramptr + 4) = (*(u32*)(vramptr + 4) & 0xCFCFCFCF) | (PPU_Planar2Linear[val & 0x000F][(val & 0x0F00) >> 8] << 4);
				break;
				
			case 0x30:
				*(u32*)vramptr = (*(u32*)vramptr & 0x3F3F3F3F) | (PPU_Planar2Linear[(val & 0x00F0) >> 4][val >> 12] << 6);
				*(u32*)(vramptr + 4) = (*(u32*)(vramptr + 4) & 0x3F3F3F3F) | (PPU_Planar2Linear[val & 0x000F][(val & 0x0F00) >> 8] << 6);
				break;
		}
	}
}

void PPU_UpdateVRAM_SCR(int nbg, u32 addr, u16 stile)
{
	PPU_Background* bg = &PPU_BG[nbg];
	u16 dtile = stile & 0x03FF;
	u16 paloffset = bg->MapPalOffset;
		
	if (stile & 0x2000)
	{
		// tile with priority
		// TODO
	}
	
	if (stile & 0x4000)
		dtile |= 0x0400;
	if (stile & 0x8000)
		dtile |= 0x0800;
	
	if (paloffset != 0xFF)
		dtile |= ((stile & 0x1C00) << 2) | paloffset;
	
	*(u16*)(BG_SCR_BASE + (nbg << 13) + addr - bg->ScrBase) = dtile;
}

void PPU_UpdateVRAM_OBJ(u32 addr, u16 val)
{
	addr -= PPU_OBJBase;
	if (addr >= 8192) addr -= PPU_OBJGap;
	u32 vramptr = OBJ_BASE;

	//vramptr += addr & 0xFFFFFFE0;
	vramptr += (addr & 0xFE00) << 1;
	vramptr += addr & 0x1E0;
	vramptr += (addr & 0xE) << 1;
	
	if (addr & 0x10)
	{
		*(u16*)vramptr = ((*(u16*)vramptr) & 0x3333) | 
			((val & 0x0080) >> 5) | ((val & 0x8000) >> 12) | 
			((val & 0x0040)) | ((val & 0x4000) >> 7) |
			((val & 0x0020) << 5) | ((val & 0x2000) >> 2) |
			((val & 0x0010) << 10) | ((val & 0x1000) << 3);
		*(u16*)(vramptr + 2) = ((*(u16*)(vramptr + 2)) & 0x3333) | 
			((val & 0x0008) >> 1) | ((val & 0x0800) >> 8) | 
			((val & 0x0004) << 4) | ((val & 0x0400) >> 3) |
			((val & 0x0002) << 9) | ((val & 0x0200) << 2) |
			((val & 0x0001) << 14) | ((val & 0x0100) << 7);
	}
	else
	{
		*(u16*)vramptr = ((*(u16*)vramptr) & 0xCCCC) | 
			((val & 0x0080) >> 7) | ((val & 0x8000) >> 14) | 
			((val & 0x0040) >> 2) | ((val & 0x4000) >> 9) |
			((val & 0x0020) << 3) | ((val & 0x2000) >> 4) |
			((val & 0x0010) << 8) | ((val & 0x1000) << 1);
		*(u16*)(vramptr + 2) = ((*(u16*)(vramptr + 2)) & 0xCCCC) | 
			((val & 0x0008) >> 3) | ((val & 0x0800) >> 10) | 
			((val & 0x0004) << 2) | ((val & 0x0400) >> 5) |
			((val & 0x0002) << 7) | ((val & 0x0200)) |
			((val & 0x0001) << 12) | ((val & 0x0100) << 5);
	}
}

void PPU_UpdateVRAM_Mode7(u32 addr, u16 val)
{
	u32 tileptr = BG_M7_BASE + ((addr & 0xFFFFFFFC) >> 1);
	u32 mapptr = tileptr + 0x4000;
	
	if (addr & 0x2)
	{
		*(u16*)tileptr = (*(u16*)tileptr & 0x00FF) | (val & 0xFF00);
		*(u16*)mapptr = (*(u16*)mapptr & 0x00FF) | (val << 8);
	}
	else
	{
		*(u16*)tileptr = (*(u16*)tileptr & 0xFF00) | (val >> 8);
		*(u16*)mapptr = (*(u16*)mapptr & 0xFF00) | (val & 0xFF);
	}
}

inline void PPU_UpdateVRAM(u32 addr, u16 val)
{
	PPU_VRAMBlock* block = &PPU_VRAMMap[addr >> 11];
	
	if (block->ChrUsage & 0x0001) PPU_UpdateVRAM_CHR(0, addr, val);
	if (block->ChrUsage & 0x0002) PPU_UpdateVRAM_CHR(1, addr, val);
	if (block->ChrUsage & 0x0004) PPU_UpdateVRAM_CHR(2, addr, val);
	if (block->ChrUsage & 0x0008) PPU_UpdateVRAM_CHR(3, addr, val);
	if (block->ChrUsage & 0x0010) PPU_UpdateVRAM_OBJ(addr, val);
	
	if (block->ScrUsage & 0x0001) PPU_UpdateVRAM_SCR(0, addr, val);
	if (block->ScrUsage & 0x0002) PPU_UpdateVRAM_SCR(1, addr, val);
	if (block->ScrUsage & 0x0004) PPU_UpdateVRAM_SCR(2, addr, val);
	if (block->ScrUsage & 0x0008) PPU_UpdateVRAM_SCR(3, addr, val);
	
	if (addr < 0x8000) PPU_UpdateVRAM_Mode7(addr, val);
	
	//if (addr < 0x1000) *(vu32*)0x040000E8 = *(vu32*)0x040000EC;
}

void PPU_UpdateOAM(u16 addr, u16 val)
{
	if (addr < 0x200)
	{
		//u16* oam = (u16*)(0x07000000 + ((addr & 0xFFFFFFFC) << 1));
		u16* oam = &PPU_OBJList[addr & 0xFFFFFFFC];
		switch (addr & 0x3)
		{
			case 0x0:	// X coord low, Y coord
				oam[0] = (oam[0] & 0xC000) | (((val >> 8) - PPU_YOffset + 1) & 0xFF);
				oam[1] = (oam[1] & 0xFF00) | (val & 0xFF);
				break;
				
			case 0x2:  	// tile num low, attrib
				{
					oam[1] = (oam[1] & 0xFFFFCFFF) | ((val & 0xC000) >> 2);
					
					oam[2] = ((val & 0x01F0) << 1) | (val & 0x000F) | ((val & 0x0E00) << 3) | 0x8000;
					
					oam[3] = (val >> 12) & 0x3;
					
					// bit0-8: tile num
					// bit9-11: pal
					// bit12-13: prio
					// bit14: Xflip
					// bit15: Yflip
				}
				break;
		}
	}
	else
	{
		addr &= 0x1F;
		//u16* oam = (u16*)(0x07000000 + (addr << 5));
		u16* oam = &PPU_OBJList[addr << 4];
		
		int i;
		for (i = 0; i < 4; i++)
		{
			u16 oval = oam[1] & 0xFFFF3EFF;
			if (val & 0x1) oval |= 0x0100;
			
			u8 objsize = PPU_OBJSizes[(val & 0x2) >> 1];
			oval |= (objsize & 0x3) << 14;
			oam[1] = oval;
			
			oval = oam[0] & 0xFFFF3FFF;
			if (objsize & 0x80) oval |= 0x8000;
			oam[0] = oval;
			
			val >>= 2;
			oam += 4;
		}
	}
}


// I/O
// addr = lowest 8 bits of address in $21xx range

u8 PPU_Read8(u32 addr)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	u8 ret = 0;
	switch (addr)
	{
		case 0x21:
			ret = PPU_CGRAMAddr;
			break;
		
		case 0x22:
			{
				u8 val = *(u8*)(0x05000000 + (PPU_CGRAMAddr << 1) + PPU_CGRFlag);
				if (PPU_CGRFlag) PPU_CGRAMAddr++;
				PPU_CGRFlag = !PPU_CGRFlag;
				ret = val;
			}
			break;
			
		case 0x34: ret = PPU_MulResult & 0xFF; break;
		case 0x35: ret = (PPU_MulResult >> 8) & 0xFF; break;
		case 0x36: ret = (PPU_MulResult >> 16) & 0xFF; break;
			
		case 0x38:
			ret = PPU_OAM[PPU_OAMAddr];
			PPU_OAMAddr++;
			break;
		
		case 0x39:
			{
				addr = (PPU_VRAMAddr << 1) & 0xFFFEFFFF;
				ret = PPU_VRAM[addr];
				if (!(PPU_VRAMInc & 0x80))
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
		case 0x3A:
			{
				addr = (PPU_VRAMAddr << 1) & 0xFFFEFFFF;
				ret = PPU_VRAM[addr + 1];
				if (PPU_VRAMInc & 0x80)
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
			
		case 0x3E: ret = 0x01; break;
		case 0x3F: ret = 0x01 | (ROM_Region ? 0x10 : 0x00); break;
		
		case 0x40: ret = IPC->SPC_IOPorts[4]; break;
		case 0x41: ret = IPC->SPC_IOPorts[5]; break;
		case 0x42: ret = IPC->SPC_IOPorts[6]; break;
		case 0x43: ret = IPC->SPC_IOPorts[7]; break;
		
		case 0x80: ret = Mem_SysRAM[Mem_WRAMAddr++]; break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
	return ret;
}

u16 PPU_Read16(u32 addr)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	u16 ret = 0;
	switch (addr)
	{
		// not in the right place, but well
		// our I/O functions are mapped to the whole $21xx range
		
		case 0x40: ret = *(u16*)&IPC->SPC_IOPorts[4]; break;
		case 0x42: ret = *(u16*)&IPC->SPC_IOPorts[6]; break;
		
		default:
			ret = PPU_Read8(addr);
			ret |= (PPU_Read8(addr+1) << 8);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
	return ret;
}

void PPU_Write8(u32 addr, u8 val)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	switch (addr)
	{
		case 0x00: // force blank/master brightness
			if (val & 0x80) val = 0;
			else val &= 0x0F;
			if (val == 0x0F)
				PPU_MasterBright = 0x0000;
			else if (val == 0x00)
				PPU_MasterBright = 0x8010;
			else
				PPU_MasterBright = 0x8000 | (15 - (val & 0x0F));
			break;
			
		case 0x01:
			{
				PPU_OBJSize = val >> 5;
				PPU_OBJSizes = _PPU_OBJSizes + (PPU_OBJSize << 1);
				u16 base = (val & 0x07) << 14;
				u16 gap = (val & 0x18) << 10;
				PPU_SetOBJCHR(base, gap);
				//iprintf("OBJ base:%08X gap:%08X | %08X\n", base, gap, (u32)&PPU_VRAM + base);
			}
			break;
			
		case 0x02:
			PPU_OAMAddr = (PPU_OAMAddr & 0x200) | (val << 1);
			break;
		case 0x03:
			PPU_OAMAddr = (PPU_OAMAddr & 0x1FE) | ((val & 0x01) << 9);
			PPU_OAMPrio = val & 0x80;
			break;
			
		case 0x04:
			if (PPU_OAMAddr >= 0x200)
			{
				u16 addr = PPU_OAMAddr;
				addr &= 0x21F;
				
				if (PPU_OAM[addr] != val)
				{
					PPU_OAM[addr] = val;
					PPU_UpdateOAM(addr, val);
					PPU_OAMDirty = 1;
				}
			}
			else if (PPU_OAMAddr & 0x1)
			{
				PPU_OAMVal |= (val << 8);
				u16 addr = PPU_OAMAddr - 1;
				
				if (*(u16*)&PPU_OAM[addr] != PPU_OAMVal)
				{
					*(u16*)&PPU_OAM[addr] = PPU_OAMVal;
					PPU_UpdateOAM(addr, PPU_OAMVal);
					PPU_OAMDirty = 1;
				}
			}
			else
			{
				PPU_OAMVal = val;
			}
			PPU_OAMAddr++;
			PPU_OAMAddr &= 0x3FF;
			break;
			
		case 0x05:
			PPU_ModeNow = val & 0x07;
			PPU_ScheduleLineChange(PPU_HandleModeChange, val);
			break;
			
		case 0x06: // mosaic
			if (val & 0x01) PPU_BG[0].BGCnt |= 0x0040;
			else PPU_BG[0].BGCnt &= 0xFFFFFFBF;
			if (val & 0x02) PPU_BG[1].BGCnt |= 0x0040;
			else PPU_BG[1].BGCnt &= 0xFFFFFFBF;
			if (val & 0x04) PPU_BG[2].BGCnt |= 0x0040;
			else PPU_BG[2].BGCnt &= 0xFFFFFFBF;
			if (val & 0x08) PPU_BG[3].BGCnt |= 0x0040;
			else PPU_BG[3].BGCnt &= 0xFFFFFFBF;
			
			/**(vu16*)0x04000008 = PPU_BG[0].BGCnt;
			*(vu16*)0x0400000A = PPU_BG[1].BGCnt;
			*(vu16*)0x0400000C = PPU_BG[2].BGCnt;
			*(vu16*)0x0400000E = PPU_BG[3].BGCnt;*/
			
			*(vu16*)0x0400004C = (val & 0xF0) | (val >> 4);
			break;
			
		case 0x07: PPU_SetBGSCR(0, val); break;
		case 0x08: PPU_SetBGSCR(1, val); break;
		case 0x09: PPU_SetBGSCR(2, val); break;
		case 0x0A: PPU_SetBGSCR(3, val); break;
			
			
		case 0x0B:
			PPU_SetBGCHR(0, val & 0x0F);
			PPU_SetBGCHR(1, val >> 4);
			break;
		case 0x0C:
			PPU_SetBGCHR(2, val & 0x0F);
			PPU_SetBGCHR(3, val >> 4);
			break;
			
		
		case 0x0D: PPU_SetXScroll(0, val); break;
		case 0x0E: PPU_SetYScroll(0, val); break;
		case 0x0F: PPU_SetXScroll(1, val); break;
		case 0x10: PPU_SetYScroll(1, val); break;
		case 0x11: PPU_SetXScroll(2, val); break;
		case 0x12: PPU_SetYScroll(2, val); break;
		case 0x13: PPU_SetXScroll(3, val); break;
		case 0x14: PPU_SetYScroll(3, val); break;
			
		
		case 0x15:
			if ((val & 0x7C) != 0x00) iprintf("UNSUPPORTED VRAM MODE %02X\n", val);
			PPU_VRAMInc = val;
			switch (val & 0x03)
			{
				case 0x00: PPU_VRAMStep = 1; break;
				case 0x01: PPU_VRAMStep = 32; break;
				case 0x02:
				case 0x03: PPU_VRAMStep = 128; break;
			}
			break;
			
		case 0x16:
			PPU_VRAMAddr &= 0xFF00;
			PPU_VRAMAddr |= val;
			PPU_VRAMPref = *(u16*)&PPU_VRAM[(PPU_VRAMAddr << 1) & 0xFFFEFFFF];
			break;
		case 0x17:
			PPU_VRAMAddr &= 0x00FF;
			PPU_VRAMAddr |= (u16)val << 8;
			PPU_VRAMPref = *(u16*)&PPU_VRAM[(PPU_VRAMAddr << 1) & 0xFFFEFFFF];
			break;
		
		case 0x18: // VRAM shit
			{
				addr = (PPU_VRAMAddr << 1) & 0xFFFEFFFF;
				if (PPU_VRAM[addr] != val)
				{
					PPU_VRAM[addr] = val;
					PPU_UpdateVRAM(addr, *(u16*)&PPU_VRAM[addr]);
				}
				if (!(PPU_VRAMInc & 0x80))
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
		case 0x19:
			{
				addr = ((PPU_VRAMAddr << 1) + 1) & 0xFFFEFFFF;
				if (PPU_VRAM[addr] != val)
				{
					PPU_VRAM[addr] = val;
					addr--;
					PPU_UpdateVRAM(addr, *(u16*)&PPU_VRAM[addr]);
				}
				if (PPU_VRAMInc & 0x80)
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
			
		case 0x1B: // multiply/mode7 shiz
			{
				u16 fval = (u16)(PPU_M7Old | (val << 8));
				PPU_MulA = (s16)fval;
				PPU_MulResult = (s32)PPU_MulA * (s32)PPU_MulB;
				PPU_ScheduleLineChange(PPU_SetM7A, fval);
				PPU_M7Old = val;
			}
			break;
		case 0x1C:
			PPU_ScheduleLineChange(PPU_SetM7B, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			PPU_MulB = (s8)val;
			PPU_MulResult = (s32)PPU_MulA * (s32)PPU_MulB;
			break;
		case 0x1D:
			PPU_ScheduleLineChange(PPU_SetM7C, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
		case 0x1E:
			PPU_ScheduleLineChange(PPU_SetM7D, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
			
		case 0x1F: // mode7 center
			PPU_ScheduleLineChange(PPU_SetM7RefX, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
		case 0x20:
			PPU_ScheduleLineChange(PPU_SetM7RefY, (val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
			
		case 0x21:
			PPU_CGRAMAddr = val;
			PPU_CGRFlag = 0;
			break;
		
		case 0x22:
			if (!PPU_CGRFlag)
			{
				PPU_CurColor = (u16)val;
			}
			else
			{
				PPU_CurColor |= (val << 8);
				register u16 paddr = PPU_CGRAMAddr << 1;
				
				if (paddr == 0) 
				{
					PPU_MainBackdrop = PPU_CurColor;
					*(u16*)0x05000000 = PPU_SubBackdrop ? PPU_SubBackdrop : PPU_MainBackdrop;
				}
				else 
					*(u16*)(0x05000000 + paddr) = PPU_CurColor;
				
				*(u16*)(0x05000200 + paddr) = PPU_CurColor;
				
				PPU_CGRAMAddr++;
				PPU_CGRDirty = 1;
			}
			PPU_CGRFlag = !PPU_CGRFlag;
			break;
		
		case 0x2C:
			PPU_ScheduleLineChange(PPU_SetMainScreen, val & 0x1F);
			break;
		case 0x2D:
			PPU_ScheduleLineChange(PPU_SetSubScreen, val & 0x1F);
			break;
			
		case 0x32:
			{
				u8 intensity = val & 0x1F;
				if (val & 0x20) PPU_SubBackdrop = (PPU_SubBackdrop & 0xFFFFFFE0) | intensity;
				if (val & 0x40) PPU_SubBackdrop = (PPU_SubBackdrop & 0xFFFFFC1F) | (intensity << 5);
				if (val & 0x80) PPU_SubBackdrop = (PPU_SubBackdrop & 0xFFFF83FF) | (intensity << 10);
				
				// TODO do this better
				*(u16*)0x05000000 = PPU_SubBackdrop ? PPU_SubBackdrop : PPU_MainBackdrop;
			}
			break;
			
		case 0x33: // SETINI
			if (val & 0x80) iprintf("!! PPU EXT SYNC\n");
			if (val & 0x40) iprintf("!! MODE7 EXTBG\n");
			if (val & 0x08) iprintf("!! PSEUDO HIRES\n");
			if (val & 0x02) iprintf("!! SMALL SPRITES\n");
			break;
			
		case 0x40: IPC->SPC_IOPorts[0] = val; break;
		case 0x41: IPC->SPC_IOPorts[1] = val; break;
		case 0x42: IPC->SPC_IOPorts[2] = val; break;
		case 0x43: IPC->SPC_IOPorts[3] = val; break;
		
		case 0x80: Mem_SysRAM[Mem_WRAMAddr++] = val; break;
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0001FF00) | val; break;
		case 0x82: Mem_WRAMAddr = (Mem_WRAMAddr & 0x000100FF) | (val << 8); break;
		case 0x83: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0000FFFF) | ((val & 0x01) << 16); break;
				
		default:
			//iprintf("PPU_Write8(%08X, %08X)\n", addr, val);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
}

void PPU_Write16(u32 addr, u16 val)
{
	asm("stmdb sp!, {r2-r3, r12}");
	
	switch (addr)
	{
		// optimized route
		
		case 0x16:
			PPU_VRAMAddr = val;
			break;
			
		case 0x40: *(u16*)&IPC->SPC_IOPorts[0] = val; break;
		case 0x41: IPC->SPC_IOPorts[1] = val & 0xFF; IPC->SPC_IOPorts[2] = val >> 8; break;
		case 0x42: *(u16*)&IPC->SPC_IOPorts[2] = val; break;
		
		case 0x43: iprintf("!! write $21%02X %04X\n", addr, val); break;
		
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x00010000) | val; break;
		
		// otherwise, just do two 8bit writes
		default:
			PPU_Write8(addr, val & 0x00FF);
			PPU_Write8(addr+1, val >> 8);
			break;
	}
	
	asm("ldmia sp!, {r2-r3, r12}");
}


ITCM_CODE void PPU_VBlank()
{
	int i;
	
	// if we're not within SNES VBlank at this time, it means we're lagging
	// and our registers are likely to contain bad values
	// (especially master brightness)
	if (PPU_VCount < 262)
	{
		PPU_MissedVBlank = true;
		return;
	}
	
	PPU_MissedVBlank = false;
	
	*(u16*)0x0400006C = PPU_MasterBright;
	
	if (PPU_CGRDirty)
	{
		PPU_CGRDirty = 0;
		
		*(u8*)0x04000245 = 0x80;
		PPU_UploadBGPal();
		*(u8*)0x04000245 = 0x8C;
	}
	
	u8 firstoam = PPU_OAMPrio ? ((PPU_OAMAddr & 0xFE) >> 1) : 0;
	if (PPU_OAMDirty || firstoam != PPU_FirstOAM)
	{
		PPU_OAMDirty = 0;
		PPU_FirstOAM = firstoam;
		
		register u32 srcaddr = firstoam << 2;
		register u16* dst = (u16*)0x07000000;
		register u8* priolist = &PPU_OBJPrioList[0];
		register int nsprites = 0;
		
		// insert faketile OBJs here
		
		for (i = 0; i < 128; i++)
		{
			u16 attr0 = PPU_OBJList[srcaddr++];
			u16 attr1 = PPU_OBJList[srcaddr++];
			
			u8 w = PPU_SpriteSize[(attr1 >> 14) | ((attr0 & 0x8000) >> 13)];
			u8 h = PPU_SpriteSize[(attr1 >> 14) | ((attr0 & 0x8000) >> 13) | 0x8];
			
			// if the sprite is offscreen, don't deal with it
			u16 y = attr0 & 0xFF;
			u16 x = attr1 & 0x1FF;
			if ((x > 0xFF && x <= (0x200 - w)) || (y > 0xBF && y <= (0x100 - h)))
			{
				srcaddr += 2;
				srcaddr &= 0x1FF;
				continue;
			}
			
			u8 prio = PPU_OBJList[srcaddr + 1];
			*priolist++ = prio;
			
			*dst++ = attr0;
			*dst++ = attr1;
			*dst++ = PPU_OBJList[srcaddr++] | PPU_OBJPrio[prio];
			*dst++ = 0x0000;
			
			srcaddr++;
			srcaddr &= 0x1FF;
			nsprites++;
		}
		
		PPU_NumSprites = nsprites;
		
		// disable all the sprites we didn't use
		for (i = nsprites; i < 128; i++)
		{
			*dst++ = 0x0200;
			*dst++ = 0x0000;
			*dst++ = 0x0000;
			*dst++ = 0x0000;
		}
	}
	
	PPU_DoLineChanges(192);
	PPU_ResetLineChanges();
}

ITCM_CODE void PPU_HBlank()
{
	u16 ds_vcount = *(vu16*)0x04000006 + 1;
	if (ds_vcount >= 263) ds_vcount = 0;
	if (ds_vcount >= 192) return;
	
	PPU_DoLineChanges(ds_vcount);
	
	// recalculate mode 7 ref point if needed
	// (ref point depends on center, scroll and A/B/C/D)
	if (PPU_Mode == 7 && PPU_M7RefDirty)
	{
		PPU_M7RefDirty = 0;
		
		s16 xscroll = PPU_M7ScrollX;
		s16 yscroll = PPU_M7ScrollY + PPU_YOffset;
		
		*(vs16*)0x04000020 = PPU_M7A;
		*(vs16*)0x04000022 = PPU_M7B;
		*(vs16*)0x04000024 = PPU_M7C;
		*(vs16*)0x04000026 = PPU_M7D;
		*(vs32*)0x04000028 = (PPU_M7RefX << 8) + (PPU_M7A * (-PPU_M7RefX + xscroll)) + (PPU_M7B * (-PPU_M7RefY + yscroll + ds_vcount));
		*(vs32*)0x0400002C = (PPU_M7RefY << 8) + (PPU_M7C * (-PPU_M7RefX + xscroll)) + (PPU_M7D * (-PPU_M7RefY + yscroll + ds_vcount));
		//iprintf("mode7 %04X|%04X %04X|%04X %04X|%04X|%04X|%04X\n",
		//	xscroll, yscroll, PPU_M7RefX, PPU_M7RefY, PPU_M7A, PPU_M7B, PPU_M7C, PPU_M7D);
	}
}
