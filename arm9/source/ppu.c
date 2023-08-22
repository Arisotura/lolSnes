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



#define BG_CHR_BASE		0x06000000
#define BG_SCR_BASE		0x06040000
#define BG_M7_BASE		0x06048000
#define OBJ_BASE		0x06400000
#define BG_PAL_BASE		0x06890000


u32 PPU_Planar2Linear[16][16];
u16 PPU_Planar2Linear16[16][16];

u8 PPU_Tilemap2Linear[256];


// TODO make this configurable
u16 PPU_YOffset = 16;

u16 PPU_VCount DTCM_DATA = 0;
bool PPU_MissedVBlank = false;

u16 PPU_OPHCT, PPU_OPVCT;
u8 PPU_OPHFlag, PPU_OPVFlag;
u8 PPU_OPLatch;

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
u16 PPU_OAMReload = 0;
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


// per-line tile priority support
// (gross hack)
// if 50% or more of the onscreen tiles on a given scanline have high prio,
// the whole BG is given high prio during the scanline
// for faster checking, we just count the number of high prio tiles per
// group of 16 tiles (128 pixels)

u16 PPU_HighPrioTiles[4][64][4] DTCM_BSS;


typedef struct
{
	u16 ChrBase;
	u16 ScrBase;
	u32 ChrSize;
	u16 ScrSize;	// size of the scr data (2K/4K/8K)
	u16 MapSize;	// BG size (0-3)
	
	int ColorDepth;
	u16 MapPalOffset;
	
	u16 ScrollX, ScrollY;
	u8 LastVOffset;
	u8 PrioStatus;
	
	u16 BGCnt;
	
	u8 DSBG;
	u8 Mask;
	
	u16* BGCNT;
	u16* BGHOFS;
	u16* BGVOFS;

} PPU_Background;
PPU_Background PPU_BG[4];

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
u8 PPU_ColorMath;

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

u8 PPU_BGStatusDirty DTCM_DATA = 0;

u8 PPU_BGOld;
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
	
	
u8* PPU_DSOBJ = NULL;		// 32K
u32 PPU_DSOBJDirty = 0;


u32 Mem_WRAMAddr;


// per-scanline change support

#define MAX_LINE_CHANGES 20

typedef struct
{
	void (*Action)(u32);
	u32 Data;
	
} PPU_LineChange;
// trick: we allocate two slots for 192 since there may be more changes on VBlank
PPU_LineChange PPU_LineChanges[192 + 2][MAX_LINE_CHANGES];
u8 PPU_NumLineChanges[193] DTCM_DATA;
u8 PPU_VBlankChangesDirty DTCM_DATA;


void PPU_SetOBJCHR(u16 base, u16 gap);

ITCM_CODE void PPU_SetM7ScrollX(u32 val);
ITCM_CODE void PPU_SetM7ScrollY(u32 val);



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
				
		PPU_Planar2Linear16[b1][b2]
				= ((b1 & 0x08) >> 3) | ((b2 & 0x08) >> 2)
				| ((b1 & 0x04) << 2) | ((b2 & 0x04) << 3)
				| ((b1 & 0x02) << 7) | ((b2 & 0x02) << 8)
				| ((b1 & 0x01) << 12) | ((b2 & 0x01) << 13);
	}
	
	for (i = 0; i < 256; i++)
	{
		// 0-63: area 0
		// 64-127: area 1
		// 128-191: area 2
		// 192-255: area 3
		
		int area_h = (i & 0x40) >> 6;
		int area_v = i >> 7;
		int offset_h = i & 0x1;
		int offset_v = (i & 0x3F) >> 1;
		
		PPU_Tilemap2Linear[i] = offset_h | (area_h << 1) | (offset_v << 2) | (area_v << 7);
	}
	
	if (!PPU_DSOBJ)
	{
		PPU_DSOBJ = (u8*)memalign(32, 0x8000);
		for (i = 0; i < 0x8000; i += 4)
			*(u32*)&PPU_DSOBJ[i] = 0;
	}
	PPU_DSOBJDirty = 0;
	
	PPU_OPHCT = 0;
	PPU_OPVCT = 0;
	PPU_OPHFlag = 0;
	PPU_OPVFlag = 0;
	PPU_OPLatch = 0;
	
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
	
	for (i = 0; i < 64*4; i++)
	{
		PPU_HighPrioTiles[0][i>>2][i&3] = 0x1000;
		PPU_HighPrioTiles[1][i>>2][i&3] = 0x1000;
		PPU_HighPrioTiles[2][i>>2][i&3] = 0x1000;
		PPU_HighPrioTiles[3][i>>2][i&3] = 0x1000;
	}
	
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
		
		bg->PrioStatus = 4;
	}
	
	
	for (i = 0; i < 193; i++)
	{
		PPU_LineChanges[i][0].Action = 0;
		PPU_NumLineChanges[i] = 0;
	}
	PPU_VBlankChangesDirty = 0;
	
	
	PPU_MasterBright = 0;
	PPU_Mode = 0;
	PPU_LastNon7Mode = -2;
	PPU_BG3Prio = 0;
	
	PPU_BGMain = 0;
	PPU_BGSub = 0;
	PPU_MainBackdrop = 0;
	PPU_SubBackdrop = 0;
	PPU_ColorMath = 0;
	
	
	PPU_OBJSize = 0;
	PPU_OBJBase = -1;
	PPU_OBJGap = -1;
	PPU_OBJSizes = _PPU_OBJSizes;
	PPU_SetOBJCHR(0, 0);
	
	for (i = 0; i < 4*128; i++)
		PPU_OBJList[i] = 0;
	
	PPU_BGStatusDirty = 0;
		
	PPU_BGOld = 0;
	PPU_M7Old = 0;
	PPU_MulA = 0;
	PPU_MulB = 0;
	PPU_MulResult = 0;
		
		
	Mem_WRAMAddr = 0;
	
	
	swiWaitForVBlank();
	vramSetBankA(VRAM_A_MAIN_BG);
	
	videoSetMode(MODE_5_2D);
	bgInit(2, BgType_Bmp16, BgSize_B16_256x256, 0,0);
	
	
	u16* barf = (u16*)0x06000000;
	for (u32 i = 0; i < 256*256; i++)
	{
		barf[i] = 0;//0x8000|i;
	}
	
	dsp_sendData(0, 0xC000);
	dsp_receiveData(0);
}


ITCM_CODE void PPU_UpdateOBJPrio()
{return;
	u16* oam = (u16*)0x07000000;
	u8* priolist = &PPU_OBJPrioList[0];
	u8* curprio = &PPU_CurPrio[12];
	u16 semitransp = (PPU_ColorMath & 0x10) ? 0x0400 : 0;
	u8 i;
	
	for (i = 0; i < PPU_NumSprites; i++)
	{
		u16 attr2 = oam[2] & 0xFFFFF3FF;
		
		if (attr2 & 0x4000) 
		{
			u16 attr0 = oam[0] & 0xFFFFF3FF;
			oam[0] = attr0 | semitransp;
		}
		
		u16 prio = curprio[*priolist++] << 10;
		oam[2] = attr2 | prio;
		
		oam += 4;
	}
}

void PPU_UpdateEnabledBGs()
{return;
	register u8 dirty = PPU_BGStatusDirty;
	int i;
	bool updateoam = (dirty & 0x10);
	
	PPU_BGStatusDirty = 0;
	
	if (dirty & 0x07)
	{
		u8 prionum;
		if ((PPU_ColorMath & 0x2F) == 0x20) // case where the subscreen ends up behind the mainscreen
			prionum = PPU_BGSub & (PPU_BGMain ^ 0x1F);
		else
			prionum = 0;
			
		u8 mode = PPU_BG3Prio ? 8 : PPU_Mode;
		
		if ((dirty & 0x01) || (prionum != PPU_CurPrioNum))
		{
			u8* newprio = &PPU_PrioTable[(mode << 9) + (prionum << 4)];
			bool reassign = (*(u32*)&newprio[0] != *(u32*)&PPU_CurPrio[0]);
			
			if (*(u32*)&newprio[12] != *(u32*)&PPU_CurPrio[12])
				updateoam = true;
			
			if (mode != 7)
			{
				for (i = 0; i < 4; i++)
				{
					PPU_Background* bg = &PPU_BG[i];
					
					bg->BGCnt = (bg->BGCnt & 0xFFFFFFFC) | newprio[bg->PrioStatus + i];
					
					if (reassign)
					{
						register u8 dsbg = newprio[i];
						bg->DSBG = dsbg;
						bg->Mask = 1 << dsbg;
						
						bg->BGCNT = (u16*)(0x04000008 + (dsbg << 1));
						bg->BGHOFS = (u16*)(0x04000010 + (dsbg << 2));
						bg->BGVOFS = (u16*)(0x04000012 + (dsbg << 2));
						
						*bg->BGHOFS = bg->ScrollX;
						*bg->BGVOFS = bg->ScrollY + PPU_YOffset;
					}
					
					*bg->BGCNT = bg->BGCnt;
				}
			}
			
			PPU_CurPrioNum = prionum;
			PPU_CurPrio = newprio;
		}
		
		PPU_BGEnable = PPU_BGMain | PPU_BGSub;
		
		u32 dispcnt = *(vu32*)0x04000000;
		dispcnt &= 0xFFFFE0FF;
		
		if (mode == 7)
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
	}
	
	if (updateoam)
		PPU_UpdateOBJPrio();
}


void PPU_ScheduleLineChange(void (*action)(u32), u32 data)
{return;
	s16 vcount = PPU_VCount + 1 - PPU_YOffset;
	u16 ds_vcount = *(vu16*)0x04000006;
	int maxchanges;
	
	// if the change occured during the vblank, schedule it for the DS vblank
	// if it occured in a hidden scanline, apply it now
	if (vcount > 191) vcount = 192;
	else if (vcount < 0)
	{
		(*action)(data);
		vcount = 192;
	}
	
	maxchanges = (vcount == 192) ? (MAX_LINE_CHANGES * 2) : MAX_LINE_CHANGES;
	
	if (vcount == 192 && PPU_VBlankChangesDirty)
	{
		PPU_VBlankChangesDirty = 0;
		PPU_NumLineChanges[192] = 0;
		//iprintf("reset vbl changes due to lag\n");
	}
	
	// if the change occured too late, apply it now, and 
	// schedule it for the next frame to make up for the lag
	if ((ds_vcount < 192 && ds_vcount >= vcount) || PPU_MissedVBlank)
		(*action)(data);
	
	// if the last change was the same thing, just overwrite its data with the new one
	u8 nchanges = PPU_NumLineChanges[vcount];
	PPU_LineChange* change;
	if (nchanges > 0)
	{
		change = &PPU_LineChanges[vcount][nchanges - 1];
		if (change->Action == action)
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
	if (nchanges < (maxchanges-1))
		PPU_NumLineChanges[vcount]++;
}

void PPU_ResetLineChanges()
{return;
	int i;
	
	for (i = 0; i < 192; i += 4)
		*(u32*)&PPU_NumLineChanges[i] = 0;
	PPU_NumLineChanges[192] = 0;
	//iprintf("reset vbl changes\n");
}

inline void PPU_DoLineChanges(u32 line)
{return;
	int i;
	register u8 limit = PPU_NumLineChanges[line];
	register PPU_LineChange* change = &PPU_LineChanges[line][0];
	//if (line==192 && limit>0) iprintf("%d vbl changes \n", limit);
	for (i = 0; i < limit; i++)
	{
		(*(change->Action))(change->Data);
		change++;
	}
	
	if (line == 192) PPU_VBlankChangesDirty = 1;
}


ITCM_CODE void PPU_SwitchToMode7(u32 loluseless)
{return;
	PPU_Mode = 7;
	PPU_BG3Prio = 0;
	u8 bgenable = (PPU_BGMain | PPU_BGSub);
	
	// switch GPU to mode 2 and use BG2
	*(u32*)0x04000000 = 0x00010002 | (4 << 24) | (4 << 27) | ((bgenable & 0x01) ? 0x400:0) | ((bgenable & 0x10) ? 0x1000:0);
	*(u16*)0x0400000C = 0xC082 | (2 << 2) | (24 << 8) | (PPU_BG[0].BGCnt & 0x0040);
	//*(u16*)0x0400000E = 0x0083 | (3 << 4) | (3 << 10);
	
	PPU_BGStatusDirty |= 0x01;
}

ITCM_CODE void PPU_SwitchFromMode7(u32 val)
{return;
	u8 newmode = val & 0x07;
	PPU_Mode = newmode;
	
	// switch GPU back to mode 0
	*(u32*)0x04000000 = 0x40010000 | (4 << 27);
	
	if ((val & 0x0F) == 9) PPU_BG3Prio = 1;
	else PPU_BG3Prio = 0;
	
	PPU_BGStatusDirty |= 0x01;

	if (PPU_LastNon7Mode != newmode)
		PPU_ModeChange(newmode);
}

ITCM_CODE void PPU_SetBG3Prio(u32 val)
{return;
	int oldprio = PPU_BG3Prio;
	if ((val & 0x0F) == 9) PPU_BG3Prio = 1;
	else PPU_BG3Prio = 0;

	if (oldprio != PPU_BG3Prio)
		PPU_BGStatusDirty |= 0x01;
}

ITCM_CODE void PPU_SetMainScreen(u32 val)
{return;
	if (PPU_BGMain == val) return;
	PPU_BGMain = val;
	PPU_BGStatusDirty |= 0x02;
}

ITCM_CODE void PPU_SetSubScreen(u32 val)
{return;
	if (PPU_BGSub == val) return;
	PPU_BGSub = val;
	PPU_BGStatusDirty |= 0x04;
}

ITCM_CODE void PPU_SetColorMath(u32 val)
{return;
	if (PPU_ColorMath == val) return;
	if ((PPU_ColorMath ^ val) & 0x10) PPU_BGStatusDirty |= 0x10;
	PPU_ColorMath = val;
	PPU_BGStatusDirty |= 0x08;
}

ITCM_CODE void PPU_SetBG1X(u32 val)
{return;
	*PPU_BG[0].BGHOFS = val & 0xFFFF;
	PPU_SetM7ScrollX(val >> 16);
}
ITCM_CODE void PPU_SetBG1Y(u32 val)
{return;
	*PPU_BG[0].BGVOFS = (val & 0xFFFF) + PPU_YOffset;
	PPU_SetM7ScrollY(val >> 16);
}

ITCM_CODE void PPU_SetBG2X(u32 val)
{return;
	*PPU_BG[1].BGHOFS = val;
}
ITCM_CODE void PPU_SetBG2Y(u32 val)
{return;
	*PPU_BG[1].BGVOFS = val + PPU_YOffset;
}

ITCM_CODE void PPU_SetBG3X(u32 val)
{return;
	*PPU_BG[2].BGHOFS = val;
}
ITCM_CODE void PPU_SetBG3Y(u32 val)
{return;
	*PPU_BG[2].BGVOFS = val + PPU_YOffset;
}

ITCM_CODE void PPU_SetBG4X(u32 val)
{return;
	*PPU_BG[3].BGHOFS = val;
}
ITCM_CODE void PPU_SetBG4Y(u32 val)
{return;
	*PPU_BG[3].BGVOFS = val + PPU_YOffset;
}

ITCM_CODE void PPU_SetM7A(u32 val)
{return;
	PPU_M7A = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7B(u32 val)
{return;
	PPU_M7B = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7C(u32 val)
{return;
	PPU_M7C = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7D(u32 val)
{return;
	PPU_M7D = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7RefX(u32 val)
{return;
	if (val & 0x1000) val |= 0xE000;
	PPU_M7RefX = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7RefY(u32 val)
{return;
	if (val & 0x1000) val |= 0xE000;
	PPU_M7RefY = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7ScrollX(u32 val)
{return;
	if (val & 0x1000) val |= 0xE000;
	PPU_M7ScrollX = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetM7ScrollY(u32 val)
{return;
	if (val & 0x1000) val |= 0xE000;
	PPU_M7ScrollY = val;
	PPU_M7RefDirty = 1;
}

ITCM_CODE void PPU_SetSubBackdrop(u32 val)
{return;
	*(u16*)0x05000000 = val ? val : PPU_MainBackdrop;
}


ITCM_CODE void PPU_UploadBGPal()
{return;
	u32* src = (u32*)0x05000000;
	u32* dst = (u32*)BG_PAL_BASE;
	u32* dst2 = dst + 0x800;
	int i;
	
	// Mode 7 not handled here (uses regular palette)
	
	switch (PPU_LastNon7Mode)
	{
		case 0:  // 4 colors, split across the 2 slots (32 palettes)
			for (i = 0; i < 32; i++)
			{
				*dst++ = *src++;
				*dst++ = *src++;
				dst += (252 >> 1);
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
{return;
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

			for (y = 0; y < 8; y++)
			{
				u8 	b1 = *bp12++,
					b2 = *bp12++;

				*dst++ = PPU_Planar2Linear[b1 >> 4][b2 >> 4];
				*dst++ = PPU_Planar2Linear[b1 & 0xF][b2 & 0xF];
			}
		}
	}
	else if (bg->ColorDepth == 5)
	{
		u8* bp12 = &PPU_VRAM[chrbase];
		u8* bp34 = bp12 + 16;
		u32* dst = (u32*)(BG_CHR_BASE + (nbg << 16));
		
		int t;
		for (t = 0; t < 1024; t++)
		{
			int y;

			for (y = 0; y < 8; y++)
			{
				u16 h1 = ((*bp12++) << 8) | (*bp34++),
					h2 = ((*bp12++) << 8) | (*bp34++);
					
				u8 b1 = ((h1 & 0x8000) >> 8) | ((h1 & 0x2000) >> 7) | ((h1 & 0x0800) >> 6) | ((h1 & 0x0200) >> 5)
				      | ((h1 & 0x0080) >> 4) | ((h1 & 0x0020) >> 3) | ((h1 & 0x0008) >> 2) | ((h1 & 0x0002) >> 1);
				u8 b2 = ((h2 & 0x8000) >> 8) | ((h2 & 0x2000) >> 7) | ((h2 & 0x0800) >> 6) | ((h2 & 0x0200) >> 5)
				      | ((h2 & 0x0080) >> 4) | ((h2 & 0x0020) >> 3) | ((h2 & 0x0008) >> 2) | ((h2 & 0x0002) >> 1);

				*dst++ = PPU_Planar2Linear[b1 >> 4][b2 >> 4];
				*dst++ = PPU_Planar2Linear[b1 & 0xF][b2 & 0xF];
			}
			
			bp12 += 16;
			bp34 += 16;
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

			for (y = 0; y < 8; y++)
			{
				u8 	b1 = *bp12++,
					b2 = *bp12++,
					b3 = *bp34++,
					b4 = *bp34++;

				*dst++ = PPU_Planar2Linear[b1 >> 4][b2 >> 4] | (PPU_Planar2Linear[b3 >> 4][b4 >> 4] << 2);
				*dst++ = PPU_Planar2Linear[b1 & 0xF][b2 & 0xF] | (PPU_Planar2Linear[b3 & 0xF][b4 & 0xF] << 2);
			}
			
			bp12 += 16;
			bp34 += 16;
		}
	}
	else if (bg->ColorDepth == 17)
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
				u16 h1 = ((*bp12++) << 8) | (*bp56++),
					h2 = ((*bp12++) << 8) | (*bp56++),
					h3 = ((*bp34++) << 8) | (*bp78++),
					h4 = ((*bp34++) << 8) | (*bp78++);
					
				u8 b1 = ((h1 & 0x8000) >> 8) | ((h1 & 0x2000) >> 7) | ((h1 & 0x0800) >> 6) | ((h1 & 0x0200) >> 5)
				      | ((h1 & 0x0080) >> 4) | ((h1 & 0x0020) >> 3) | ((h1 & 0x0008) >> 2) | ((h1 & 0x0002) >> 1);
				u8 b2 = ((h2 & 0x8000) >> 8) | ((h2 & 0x2000) >> 7) | ((h2 & 0x0800) >> 6) | ((h2 & 0x0200) >> 5)
				      | ((h2 & 0x0080) >> 4) | ((h2 & 0x0020) >> 3) | ((h2 & 0x0008) >> 2) | ((h2 & 0x0002) >> 1);
				u8 b3 = ((h3 & 0x8000) >> 8) | ((h3 & 0x2000) >> 7) | ((h3 & 0x0800) >> 6) | ((h3 & 0x0200) >> 5)
				      | ((h3 & 0x0080) >> 4) | ((h3 & 0x0020) >> 3) | ((h3 & 0x0008) >> 2) | ((h3 & 0x0002) >> 1);
				u8 b4 = ((h4 & 0x8000) >> 8) | ((h4 & 0x2000) >> 7) | ((h4 & 0x0800) >> 6) | ((h4 & 0x0200) >> 5)
				      | ((h4 & 0x0080) >> 4) | ((h4 & 0x0020) >> 3) | ((h4 & 0x0008) >> 2) | ((h4 & 0x0002) >> 1);

				*dst++ = PPU_Planar2Linear[b1 >> 4][b2 >> 4] | (PPU_Planar2Linear[b3 >> 4][b4 >> 4] << 2);
				*dst++ = PPU_Planar2Linear[b1 & 0xF][b2 & 0xF] | (PPU_Planar2Linear[b3 & 0xF][b4 & 0xF] << 2);
			}
			
			bp12 += 48;
			bp34 += 48;
			bp56 += 48;
			bp78 += 48;
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
{return;
	PPU_Background* bg = &PPU_BG[nbg];
	u8* tm2l = &PPU_Tilemap2Linear[0];
	u16* hptiles = (u16*)&PPU_HighPrioTiles[nbg];
	bool thinbg = !(bg->BGCnt & 0x4000);
	
	if (bg->ColorDepth == 0)
		return;
		
	bool hires = bg->ColorDepth & 1;
	
	int bgsize = bg->ScrSize >> 1;
	u16* src = (u16*)&PPU_VRAM[bg->ScrBase];
	u16* dst = (u16*)(BG_SCR_BASE + (nbg << 13));
	register u16 palbase = bg->MapPalOffset;
	
	int t;
	u16 hpt = 0;
	for (t = 0; t < bgsize; t++)
	{
		u16 stile = *src++;
		u16 dtile = stile & 0x03FF;
		
		if (hires) dtile >>= 1;
		
		if (stile == 0) hpt += 0x0100; // dirty hack
		else
		{
			// since we are counting up to 16 tiles, this won't cause overflow
			if (stile & 0x2000) hpt += 0x0001;
			
			if (stile & 0x4000)
				dtile |= 0x0400;
			if (stile & 0x8000)
				dtile |= 0x0800;
			
			if (palbase != 0xFF)
				dtile |= ((stile & 0x1C00) << 2) | palbase;
		}

		*dst++ = dtile;

		if ((t & 0xF) == 0xF)
		{
			hptiles[*tm2l] = hpt;
			if (thinbg)
				hptiles[(*tm2l) + 2] = hpt;
			
			tm2l++;
			hpt = 0;
			
			if (thinbg && ((t & 0x3FF) == 0x3FF))
				tm2l += 64;
		}
	}
}

void PPU_UploadOBJChr()
{return;
	u16 chrbase = PPU_OBJBase;
	
	u8* bp12 = &PPU_VRAM[chrbase];
	u8* bp34 = bp12 + 16;
	//u16* dst = (u16*)OBJ_BASE;
	u16* dst = (u16*)PPU_DSOBJ;
	
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

			*dst++ = PPU_Planar2Linear16[b1 >> 4][b2 >> 4] | (PPU_Planar2Linear16[b3 >> 4][b4 >> 4] << 2);
			*dst++ = PPU_Planar2Linear16[b1 & 0xF][b2 & 0xF] | (PPU_Planar2Linear16[b3 & 0xF][b4 & 0xF] << 2);
		}
		
		bp12 += 16;
		bp34 += 16;
		
		if ((t & 0xF) == 0xF)
			dst += 256;
	}
	
	PPU_DSOBJDirty = 0xFFFFFFFF;
}

void PPU_SetupBG(int nbg, int depth, u16 mappaloffset)
{return;
	PPU_Background* bg = &PPU_BG[nbg];
	
	register u8 dsbg = PPU_CurPrio[nbg];
	
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
			case 4: 
			case 5: bg->ChrSize = 0x4000; break;
			case 16: 
			case 17: bg->ChrSize = 0x8000; break;
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

		if (depth != 0)
		{
			PPU_UploadBGChr(nbg);
			if (olddepth == 0 || bg->MapPalOffset != mappaloffset || ((olddepth ^ depth) & 1)) 
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
	}
	
	*bg->BGCNT = bg->BGCnt;
}

void PPU_ModeChange(u8 newmode)
{return;
	PPU_LastNon7Mode = newmode;
	iprintf("[%03d] PPU mode %d\n", *(vu16*)0x04000006, newmode);
	switch (newmode)
	{
		case 0:
			PPU_SetupBG(0, 4, 0);
			PPU_SetupBG(1, 4, 0x8000);
			PPU_SetupBG(2, 4, 0);
			PPU_SetupBG(3, 4, 0x8000);
			break;
		
		case 1:
			PPU_SetupBG(0, 16, 0);
			PPU_SetupBG(1, 16, 0);
			PPU_SetupBG(2, 4, 0x8000);
			PPU_SetupBG(3, 0, 0);
			break;
			
		case 2:
			PPU_SetupBG(0, 16, 0);
			PPU_SetupBG(1, 16, 0);
			PPU_SetupBG(2, 0, 0);	// TODO OFFSET PER TILE
			PPU_SetupBG(3, 0, 0);
			break;
			
		case 3:
			PPU_SetupBG(0, 256, 0xFF);
			PPU_SetupBG(1, 16, 0);
			PPU_SetupBG(2, 0, 0);
			PPU_SetupBG(3, 0, 0);
			break;
			
		case 4:
			PPU_SetupBG(0, 256, 0xFF);
			PPU_SetupBG(1, 4, 0);
			PPU_SetupBG(2, 0, 0);  // TODO OFFSET PER TILE
			PPU_SetupBG(3, 0, 0);
			break;
			
		case 5:
			PPU_SetupBG(0, 17, 0);
			PPU_SetupBG(1, 5, 0x8000);
			PPU_SetupBG(2, 0, 0);
			PPU_SetupBG(3, 0, 0);
			break;
			
		case 6:
			PPU_SetupBG(0, 17, 0);
			PPU_SetupBG(1, 0, 0);
			PPU_SetupBG(2, 0, 0);	// TODO OFFSET PER TILE
			PPU_SetupBG(3, 0, 0);
			break;
	}
	
	PPU_CGRDirty = 1;
}


inline void PPU_SetXScroll(int nbg, u8 val, void (*func)(u32))
{return;
	u32 m7stuff = 0;
	
	if (nbg == 0)
	{
		m7stuff = (val << 8) | PPU_M7Old;
		PPU_M7Old = val;
		
		if (PPU_ModeNow == 7)
		{
			PPU_ScheduleLineChange(PPU_SetM7ScrollX, m7stuff);
			return;
		}
	}
	
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->ScrollX = (val << 8) | (PPU_BGOld & 0xFFF8) | ((bg->ScrollX >> 8) & 0x7);
	PPU_BGOld = val;
	
	PPU_ScheduleLineChange(func, bg->ScrollX | (m7stuff << 16));
}

inline void PPU_SetYScroll(int nbg, u8 val, void (*func)(u32))
{return;
	u32 m7stuff = 0;
	
	if (nbg == 0)
	{
		m7stuff = (val << 8) | PPU_M7Old;
		PPU_M7Old = val;
		
		if (PPU_ModeNow == 7)
		{
			PPU_ScheduleLineChange(PPU_SetM7ScrollY, m7stuff);
			return;
		}
	}
	
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->ScrollY = (val << 8) | PPU_BGOld;
	PPU_BGOld = val;
	
	PPU_ScheduleLineChange(func, bg->ScrollY | (m7stuff << 16));
}

inline void PPU_SetBGSCR(int nbg, u8 val)
{return;
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
	
	bg->BGCnt &= 0xFFFF3FFF;
	bg->BGCnt |= ((val & 0x03) << 14);
	
	if (PPU_ModeNow != 7)
		*bg->BGCNT = bg->BGCnt;
	
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
}

inline void PPU_SetBGCHR(int nbg, u8 val)
{return;
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

void PPU_SetOBJCHR(u16 base, u16 gap)
{return;
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
	}
	else if (bg->ColorDepth == 5)
	{
		vramptr += (addr & 0xFFFFFFE0) << 1;
		vramptr += (addr & 0xE) << 2;
		
		val = ((val & 0x8000) >> 8) | ((val & 0x2000) >> 7) | ((val & 0x0800) >> 6) | ((val & 0x0200) >> 5)
		    | ((val & 0x0080) >> 4) | ((val & 0x0020) >> 3) | ((val & 0x0008) >> 2) | ((val & 0x0002) >> 1);
		
		if (addr & 0x10)
		{
			*(u32*)(vramptr + 4) = PPU_Planar2Linear[val & 0x0F][val >> 4];
		}
		else
		{
			*(u32*)vramptr = PPU_Planar2Linear[val & 0x0F][val >> 4];
		}
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
	else if (bg->ColorDepth == 17)
	{
		vramptr += addr & 0xFFFFFFC0;
		vramptr += (addr & 0xE) << 2;
		
		val = ((val & 0x8000) >> 8) | ((val & 0x2000) >> 7) | ((val & 0x0800) >> 6) | ((val & 0x0200) >> 5)
		    | ((val & 0x0080) >> 4) | ((val & 0x0020) >> 3) | ((val & 0x0008) >> 2) | ((val & 0x0002) >> 1);
		
		switch (addr & 0x30)
		{
			case 0x00:
				*(u32*)vramptr = (*(u32*)vramptr & 0xFCFCFCFC) | PPU_Planar2Linear[val & 0x0F][val >> 4];
				break;
			
			case 0x10:
				*(u32*)vramptr = (*(u32*)vramptr & 0xF3F3F3F3) | (PPU_Planar2Linear[val & 0x0F][val >> 4] << 2);
				break;
				
			case 0x20:
				*(u32*)(vramptr + 4) = (*(u32*)(vramptr + 4) & 0xFCFCFCFC) | PPU_Planar2Linear[val & 0x0F][val >> 4];
				break;
				
			case 0x30:
				*(u32*)(vramptr + 4) = (*(u32*)(vramptr + 4) & 0xF3F3F3F3) | (PPU_Planar2Linear[val & 0x0F][val >> 4] << 2);
				break;
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

void PPU_UpdateVRAM_SCR(int nbg, u32 addr, u16 oldtile, u16 stile)
{
	PPU_Background* bg = &PPU_BG[nbg];
	u16 dtile = stile & 0x03FF;
	u16 paloffset = bg->MapPalOffset;

	if (bg->ColorDepth & 1) dtile >>= 1;
	
	u32 offset = addr - bg->ScrBase;
	bool thinbg = !(bg->BGCnt & 0x4000);
	u16* hptiles;
	
	if (thinbg)
		hptiles = &PPU_HighPrioTiles[nbg][0][0] + PPU_Tilemap2Linear[(offset + (offset & 0x800)) >> 5];
	else
		hptiles = &PPU_HighPrioTiles[nbg][0][0] + PPU_Tilemap2Linear[offset >> 5];
		
	if (oldtile == 0)
		(*hptiles) -= 0x0100;
	else if (oldtile & 0x2000)
		(*hptiles) -= 0x0001;
		
	if (stile == 0)
	{
		(*hptiles) += 0x0100;
	}
	else
	{
		if (stile & 0x2000) (*hptiles) += 0x0001;
		
		if (stile & 0x4000)
			dtile |= 0x0400;
		if (stile & 0x8000)
			dtile |= 0x0800;
		
		if (paloffset != 0xFF)
			dtile |= ((stile & 0x1C00) << 2) | paloffset;
	}
	
	if (thinbg)
		*(hptiles + 2) = *hptiles;
	
	*(u16*)(BG_SCR_BASE + (nbg << 13) + offset) = dtile;
}

void PPU_UpdateVRAM_OBJ(u32 addr, u16 val)
{
	addr -= PPU_OBJBase;
	if (addr >= 8192) addr -= PPU_OBJGap;
	//u32 vramptr = OBJ_BASE;
	u8* vramptr = PPU_DSOBJ;

	u32 offset = ((addr & 0xFFFFFE0E) << 1) + (addr & 0x1E0);
	vramptr += offset;
	
	if (addr & 0x10)
	{
		*(u16*)vramptr = (*(u16*)vramptr & 0x3333) | (PPU_Planar2Linear16[(val & 0x00F0) >> 4][val >> 12] << 2);
		*(u16*)(vramptr + 2) = (*(u16*)(vramptr + 2) & 0x3333) | (PPU_Planar2Linear16[val & 0x000F][(val & 0x0F00) >> 8] << 2);
	}
	else
	{
		*(u16*)vramptr = (*(u16*)vramptr & 0xCCCC) | PPU_Planar2Linear16[(val & 0x00F0) >> 4][val >> 12];
		*(u16*)(vramptr + 2) = (*(u16*)(vramptr + 2) & 0xCCCC) | PPU_Planar2Linear16[val & 0x000F][(val & 0x0F00) >> 8];
	}
	
	PPU_DSOBJDirty |= (1 << (offset >> 10));
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

inline void PPU_UpdateVRAM(u32 addr, u16 oldval, u16 val)
{return;
	PPU_VRAMBlock* block = &PPU_VRAMMap[addr >> 11];
	
	if (block->ChrUsage & 0x0001) PPU_UpdateVRAM_CHR(0, addr, val);
	if (block->ChrUsage & 0x0002) PPU_UpdateVRAM_CHR(1, addr, val);
	if (block->ChrUsage & 0x0004) PPU_UpdateVRAM_CHR(2, addr, val);
	if (block->ChrUsage & 0x0008) PPU_UpdateVRAM_CHR(3, addr, val);
	if (block->ChrUsage & 0x0010) PPU_UpdateVRAM_OBJ(addr, val);
	
	if (block->ScrUsage & 0x0001) PPU_UpdateVRAM_SCR(0, addr, oldval, val);
	if (block->ScrUsage & 0x0002) PPU_UpdateVRAM_SCR(1, addr, oldval, val);
	if (block->ScrUsage & 0x0004) PPU_UpdateVRAM_SCR(2, addr, oldval, val);
	if (block->ScrUsage & 0x0008) PPU_UpdateVRAM_SCR(3, addr, oldval, val);
	
	if (addr < 0x8000) PPU_UpdateVRAM_Mode7(addr, val);
}

void PPU_UpdateOAM(u16 addr, u16 val)
{
	if (addr < 0x200)
	{
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


void PPU_LatchHVCounters()
{
	// TODO simulate this one based on CPU cycle counter
	PPU_OPHCT = 22;
	
	PPU_OPVCT = 1 + PPU_VCount;
	if (PPU_OPVCT > 261) PPU_OPVCT = 0;
	
	PPU_OPLatch = 0x40;
}


// I/O
// addr = lowest 8 bits of address in $21xx range

u8 PPU_Read8(u32 addr)
{
	u8 ret = 0;
	switch (addr)
	{
		case 0x34: ret = PPU_MulResult & 0xFF; break;
		case 0x35: ret = (PPU_MulResult >> 8) & 0xFF; break;
		case 0x36: ret = (PPU_MulResult >> 16) & 0xFF; break;
		
		case 0x37:
			PPU_LatchHVCounters();
			break;
			
		case 0x38:
			ret = PPU_OAM[PPU_OAMAddr];
			PPU_OAMAddr++;
			break;
		
		case 0x39:
			{
				ret = PPU_VRAMPref & 0xFF;
				if (!(PPU_VRAMInc & 0x80))
				{
					PPU_VRAMPref = *(u16*)&PPU_VRAM[(PPU_VRAMAddr << 1) & 0xFFFEFFFF];
					PPU_VRAMAddr += PPU_VRAMStep;
				}
			}
			break;
		case 0x3A:
			{
				ret = PPU_VRAMPref >> 8;
				if (PPU_VRAMInc & 0x80)
				{
					PPU_VRAMPref = *(u16*)&PPU_VRAM[(PPU_VRAMAddr << 1) & 0xFFFEFFFF];
					PPU_VRAMAddr += PPU_VRAMStep;
				}
			}
			break;
			
		case 0x3C:
			if (PPU_OPHFlag)
			{
				PPU_OPHFlag = 0;
				ret = PPU_OPHCT >> 8;
			}
			else
			{
				PPU_OPHFlag = 1;
				ret = PPU_OPHCT & 0xFF;
			}
			break;
		case 0x3D:
			if (PPU_OPVFlag)
			{
				PPU_OPVFlag = 0;
				ret = PPU_OPVCT >> 8;
			}
			else
			{
				PPU_OPVFlag = 1;
				ret = PPU_OPVCT & 0xFF;
			}
			break;
			
		case 0x3E: ret = 0x01; break;
		case 0x3F: 
			ret = 0x01 | (ROM_Region ? 0x10 : 0x00) | PPU_OPLatch;
			PPU_OPLatch = 0;
			PPU_OPHFlag = 0;
			PPU_OPVFlag = 0;
			break;
		
		case 0x40: ret = IPC->SPC_IOPorts[4]; break;
		case 0x41: ret = IPC->SPC_IOPorts[5]; break;
		case 0x42: ret = IPC->SPC_IOPorts[6]; break;
		case 0x43: ret = IPC->SPC_IOPorts[7]; break;
		
		case 0x80: ret = Mem_SysRAM[Mem_WRAMAddr++]; break;
	}

	return ret;
}

u16 PPU_Read16(u32 addr)
{
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

	return ret;
}
int numio=0;
void PPU_Write8(u32 addr, u8 val)
{
	if (addr < 0x40)
	{//numio++;
		// send write to DSP
		dsp_sendData(0, (addr<<8) | val);
	}
	
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
			PPU_OAMReload = PPU_OAMAddr;
			break;
		case 0x03:
			PPU_OAMAddr = (PPU_OAMAddr & 0x1FE) | ((val & 0x01) << 9);
			PPU_OAMPrio = val & 0x80;
			PPU_OAMReload = PPU_OAMAddr;
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
			if (PPU_ModeNow != PPU_Mode)
			{
				if (PPU_Mode == 7) 
				{
					PPU_ScheduleLineChange(PPU_SwitchFromMode7, val);
					break;
				}
				else if (PPU_ModeNow == 7) 
				{
					PPU_ScheduleLineChange(PPU_SwitchToMode7, 0);
					break;
				}
			}
			if (val & 0xF0) iprintf("!! 16x16 TILES NOT SUPPORTED\n");
			PPU_ScheduleLineChange(PPU_SetBG3Prio, val);
			break;
			
		case 0x06: // mosaic
			{
				if (PPU_ModeNow == 7)
				{
					*(u16*)0x0400000C = 0xC082 | (2 << 2) | (24 << 8) | (val & 0x01) << 6;
				}
				else
				{
					int i;
					for (i = 0; i < 4; i++)
					{
						PPU_Background* bg = &PPU_BG[i];
						if (val & (1 << i)) bg->BGCnt |= 0x0040;
						else bg->BGCnt &= 0xFFFFFFBF;
						
						*bg->BGCNT = bg->BGCnt;
					}
				}
			
				*(vu16*)0x0400004C = (val & 0xF0) | (val >> 4);
			}
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
			
		
		case 0x0D: PPU_SetXScroll(0, val, PPU_SetBG1X); break;
		case 0x0E: PPU_SetYScroll(0, val, PPU_SetBG1Y); break;
		case 0x0F: PPU_SetXScroll(1, val, PPU_SetBG2X); break;
		case 0x10: PPU_SetYScroll(1, val, PPU_SetBG2Y); break;
		case 0x11: PPU_SetXScroll(2, val, PPU_SetBG3X); break;
		case 0x12: PPU_SetYScroll(2, val, PPU_SetBG3Y); break;
		case 0x13: PPU_SetXScroll(3, val, PPU_SetBG4X); break;
		case 0x14: PPU_SetYScroll(3, val, PPU_SetBG4Y); break;
			
		
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
					u16 oldval = *(u16*)&PPU_VRAM[addr];
					PPU_VRAM[addr] = val;
					PPU_UpdateVRAM(addr, oldval, *(u16*)&PPU_VRAM[addr]);
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
					u16 oldval = *(u16*)&PPU_VRAM[addr-1];
					PPU_VRAM[addr] = val;
					addr--;
					PPU_UpdateVRAM(addr, oldval, *(u16*)&PPU_VRAM[addr]);
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
			
		case 0x31:
			PPU_ScheduleLineChange(PPU_SetColorMath, val);
			break;
			
		case 0x32:
			{
				u8 intensity = val & 0x1F;
				if (val & 0x20) PPU_SubBackdrop = (PPU_SubBackdrop & 0xFFFFFFE0) | intensity;
				if (val & 0x40) PPU_SubBackdrop = (PPU_SubBackdrop & 0xFFFFFC1F) | (intensity << 5);
				if (val & 0x80) PPU_SubBackdrop = (PPU_SubBackdrop & 0xFFFF83FF) | (intensity << 10);
				
				PPU_ScheduleLineChange(PPU_SetSubBackdrop, PPU_SubBackdrop);
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
}

void PPU_Write16(u32 addr, u16 val)
{
	switch (addr)
	{
		// optimized route
		
		/*case 0x16:
			PPU_VRAMAddr = val;
			PPU_VRAMPref = *(u16*)&PPU_VRAM[(PPU_VRAMAddr << 1) & 0xFFFEFFFF];
			break;*/
			
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
}


ITCM_CODE void PPU_SNESVBlank()
{
	//iprintf("IO: %d\n", numio); numio = 0;
	PPU_OAMAddr = PPU_OAMReload;
	dsp_sendData(0, 0x8000);
}

ITCM_CODE void PPU_SNESVBlankEnd()
{
	//
}

ITCM_CODE void PPU_SNESHBlank()
{numio++;
	dsp_sendData(0, 0x4000 | (PPU_VCount & 0x1FF));
}

u32 gorp = 0;
ITCM_CODE void PPU_VBlank()
{
	int i;
	//iprintf("%04X\n", *(vu16*)0x06000000);
	u32 v = *(vu32*)0x06001000;
	if (v > gorp) gorp = v;
	iprintf("%08X %08X\n", v, gorp);
	//iprintf("IO: %d\n", numio); numio = 0;
	return;
	// if we're not within SNES VBlank at this time, it means we're lagging
	// and our registers are likely to contain bad values
	// (especially master brightness)
	if (PPU_VCount < 262)
	{
		//iprintf("VBlank [miss]\n");
		PPU_MissedVBlank = true;
		PPU_DoLineChanges(192);
		return;
	}
	
	//iprintf("VBlank\n");
	PPU_MissedVBlank = false;
	
	*(u16*)0x0400006C = PPU_MasterBright;
	
	if (PPU_ModeNow != 7 && PPU_ModeNow != PPU_LastNon7Mode)
	{
		PPU_Mode = PPU_ModeNow;
		
		PPU_BGStatusDirty |= 0x01;
		PPU_ModeChange(PPU_ModeNow);
		PPU_UpdateEnabledBGs();
	}
	
	if (PPU_CGRDirty)
	{
		PPU_CGRDirty = 0;
		
		*(u8*)0x04000245 = 0x80;
		PPU_UploadBGPal();
		*(u8*)0x04000245 = 0x8C;
	}
	
	if (PPU_DSOBJDirty)
	{
		u8* src = PPU_DSOBJ;
		u8* dst = (u8*)OBJ_BASE;
		u32 offset = 0, size;
		
		while (PPU_DSOBJDirty)
		{
			size = 0;
			
			while (!(PPU_DSOBJDirty & 1))
			{
				offset += 1024;
				PPU_DSOBJDirty >>= 1;
			}
			
			while (PPU_DSOBJDirty & 1)
			{
				size += 1024;
				PPU_DSOBJDirty >>= 1;
			}
			
			DC_FlushRange(src + offset, size);
			dmaCopyWords(3, src + offset, dst + offset, size);
			offset += size;
		}
	}
	
	u8 firstoam = PPU_OAMPrio ? ((PPU_OAMAddr & 0xFE) >> 1) : 0;
	if (PPU_OAMDirty || firstoam != PPU_FirstOAM)
	{
		PPU_OAMDirty = 0;
		PPU_FirstOAM = firstoam;
		
		register u32 srcaddr = firstoam << 2;
		register u32* dst = (u32*)0x07000000;
		register u8* priolist = &PPU_OBJPrioList[0];
		register int nsprites = 0;
		register u8* sprprio = &PPU_CurPrio[12];
		u16 semitransp = (PPU_ColorMath & 0x10) ? 0x0400 : 0;
		
		for (i = 0; i < 128; i++)
		{
			u32 attr01 = *(u32*)&PPU_OBJList[srcaddr];
			srcaddr += 2;
			u32 attr2 = *(u32*)&PPU_OBJList[srcaddr];
			srcaddr += 2;
			srcaddr &= 0x1FF;
			
			u8 w = PPU_SpriteSize[(attr01 >> 30) | ((attr01 & 0x8000) >> 13)];
			u8 h = PPU_SpriteSize[(attr01 >> 30) | ((attr01 & 0x8000) >> 13) | 0x8];
			
			// if the sprite is offscreen, don't deal with it
			u16 y = attr01 & 0xFF;
			u16 x = (attr01 >> 16) & 0x1FF;
			if ((x > 0xFF && x <= (0x200 - w)) || (y > 0xBF && y <= (0x100 - h)))
				continue;
			
			u32 prio = attr2 >> 16;
			*priolist++ = prio;
			attr2 &= 0xFFFF;
			
			if (attr2 & 0x4000) attr01 |= semitransp;
			
			*dst++ = attr01;
			*dst++ = attr2 | (sprprio[prio] << 10);
			
			nsprites++;
		}
		
		PPU_NumSprites = nsprites;
		
		// disable all the sprites we didn't use
		for (i = nsprites; i < 128; i++)
		{
			*dst++ = 0x00000200;
			*dst++ = 0x00000000;
		}
	}
	
	PPU_DoLineChanges(192);
	PPU_ResetLineChanges();
}

ITCM_CODE void PPU_HBlank()
{return;
	u16 ds_vcount = *(vu16*)0x04000006 + 1;
	if (ds_vcount >= 263) ds_vcount = 0;
	if (ds_vcount >= 192) return;
	u16 yoffset = ds_vcount + PPU_YOffset;
	
	PPU_DoLineChanges(ds_vcount);
	
	// recalculate mode 7 ref point if needed
	// (ref point depends on center, scroll and A/B/C/D)
	if (PPU_Mode == 7 && PPU_M7RefDirty)
	{
		PPU_M7RefDirty = 0;
		
		s16 xscroll = PPU_M7ScrollX;
		s16 yscroll = PPU_M7ScrollY;
		
		*(vs16*)0x04000020 = PPU_M7A;
		*(vs16*)0x04000022 = PPU_M7B;
		*(vs16*)0x04000024 = PPU_M7C;
		*(vs16*)0x04000026 = PPU_M7D;
		*(vs32*)0x04000028 = (PPU_M7RefX << 8) + (PPU_M7A * (-PPU_M7RefX + xscroll)) + (PPU_M7B * (-PPU_M7RefY + yscroll + yoffset));
		*(vs32*)0x0400002C = (PPU_M7RefY << 8) + (PPU_M7C * (-PPU_M7RefX + xscroll)) + (PPU_M7D * (-PPU_M7RefY + yscroll + yoffset));
		//iprintf("[%03d] mode7 %04X|%04X %04X|%04X %04X|%04X|%04X|%04X\n",
		//	yoffset, xscroll, yscroll, PPU_M7RefX, PPU_M7RefY, PPU_M7A, PPU_M7B, PPU_M7C, PPU_M7D);
	}
	
	if (PPU_Mode != 7)
	{
		PPU_Background* bg;
		u16* hptiles = (u16*)&PPU_HighPrioTiles[1];
		int i;
		for (bg = &PPU_BG[1], i = 1; bg < &PPU_BG[4] && bg->ColorDepth > 0; bg++, i++)
		{
			u16 voffset = ((bg->ScrollY + yoffset) >> 3) & ((bg->BGCnt & 0x8000) ? 0x3F : 0x1F);
			//if (voffset == bg->LastVOffset)
			//	continue;
			
			bg->LastVOffset = voffset;
			u16 hoffset = bg->ScrollX >> 6;
			u16* hpt = &hptiles[voffset << 2];
			
			u16 subhoffset = hoffset & 1;
			hoffset >>= 1;
			int tilesum = hpt[(hoffset + 1) & 3];
			if (subhoffset) tilesum += hpt[(hoffset + 2) & 3];
			else tilesum += hpt[hoffset & 3];
			
			u8 ps;
			if ((tilesum & 0x00FF) && (((tilesum & 0xFF) + (tilesum >> 8)) >= 16))
				ps = 8;
			else
				ps = 4;
			
			if (ps != bg->PrioStatus)
			{
				bg->PrioStatus = ps;
				
				bg->BGCnt = (bg->BGCnt & 0xFFFFFFFC) | PPU_CurPrio[ps + i];
				*bg->BGCNT = bg->BGCnt;
			}
			
			hptiles += 64*4;
		}
	}
	
	if (PPU_BGStatusDirty) 
		PPU_UpdateEnabledBGs();
}
