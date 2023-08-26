#include "teak/teak.h"
#include "ppu.h"
#include "ipc.h"


PPU_State PPU;
u16 PPU_Output[256*2];
u16 PPU_CurOutput;

u16 PPU_CmdFIFO[1024];

#define PPU_VRAM(addr) (*(u16*)((addr) & 0x7FFF))


void timer_start(void)
{
	*(vu16*)0x8020 = (2<<2)|(1<<9)|(1<<10);
}

u32 timer_read(void)
{
	u16 r1 = *(vu16*)0x8028;
	u16 r2 = *(vu16*)0x802A;
	u32 ret = r1 | (r2<<16);
	return ret;
}


void PPU_Reset(void)
{
	u16 state = (u16)&PPU;
	for (u16 i = 0; i < sizeof(PPU)/2; i++)
		*(vu16*)i = 0;
	for (u16 i = 0; i < 256*2; i++)
		PPU_Output[i] = 0;
	
	PPU_CurOutput = 0;
}


void PPU_SetOBJCnt(u16 val)
{
	PPU.OBJChr = (val & 0x03) << 13;
	PPU.OBJGap = (val & 0x18) << 9;
	
	switch (val >> 5)
	{
	case 0: PPU.OBJWidth[0] = 8; PPU.OBJHeight[0] = 8; PPU.OBJWidth[1] = 16; PPU.OBJHeight[1] = 16; break;
	case 1: PPU.OBJWidth[0] = 8; PPU.OBJHeight[0] = 8; PPU.OBJWidth[1] = 32; PPU.OBJHeight[1] = 32; break;
	case 2: PPU.OBJWidth[0] = 8; PPU.OBJHeight[0] = 8; PPU.OBJWidth[1] = 64; PPU.OBJHeight[1] = 64; break;
	case 3: PPU.OBJWidth[0] = 16; PPU.OBJHeight[0] = 16; PPU.OBJWidth[1] = 32; PPU.OBJHeight[1] = 32; break;
	case 4: PPU.OBJWidth[0] = 16; PPU.OBJHeight[0] = 16; PPU.OBJWidth[1] = 64; PPU.OBJHeight[1] = 64; break;
	case 5: PPU.OBJWidth[0] = 32; PPU.OBJHeight[0] = 32; PPU.OBJWidth[1] = 64; PPU.OBJHeight[1] = 64; break;
	case 6: PPU.OBJWidth[0] = 16; PPU.OBJHeight[0] = 32; PPU.OBJWidth[1] = 32; PPU.OBJHeight[1] = 64; break;
	case 7: PPU.OBJWidth[0] = 16; PPU.OBJHeight[0] = 32; PPU.OBJWidth[1] = 32; PPU.OBJHeight[1] = 32; break;
	}
}

void PPU_SetBGScr(u16 num, u16 val)
{
	PPU.BGScr[num] = (val & 0x7C) << 8;
	
	switch (val & 0x3)
	{
	case 0: PPU.BGXScr2[num] = 0;    PPU.BGYScr2[num] = 0;    break;
	case 1: PPU.BGXScr2[num] = 1024; PPU.BGYScr2[num] = 0;    break;
	case 2: PPU.BGXScr2[num] = 0;    PPU.BGYScr2[num] = 1024; break;
	case 3: PPU.BGXScr2[num] = 1024; PPU.BGYScr2[num] = 2048; break;
	}
}

void PPU_SetBGXPos(u16 num, u16 val)
{
	PPU.BGXPos[num] = (val << 8) | (PPU.BGOld & 0xF8) | ((PPU.BGXPos[num] >> 8) & 0x7);
	PPU.BGOld = val;
}

void PPU_SetBGYPos(u16 num, u16 val)
{
	PPU.BGYPos[num] = (val << 8) | PPU.BGOld;
	PPU.BGOld = val;
}

u16 PPU_TranslateVRAMAddress(u16 addr)
{
	switch (PPU.VRAMInc & 0x0C)
	{
	case 0x00: 
		return addr & 0x7FFF;
	
	case 0x04:
		return (addr & 0x7F00) |
			  ((addr & 0x00E0) >> 5) |
			  ((addr & 0x001F) << 3);
			  
	case 0x08:
		return (addr & 0x7E00) |
			  ((addr & 0x01C0) >> 6) |
			  ((addr & 0x003F) << 3);
			  
	case 0x0C:
		return (addr & 0x7C00) |
			  ((addr & 0x0380) >> 7) |
			  ((addr & 0x007F) << 3);
	}
	
	// herp
	return addr & 0x7FFF;
}

void PPU_Write8(u16 addr, u16 val)
{
	switch (addr)
	{
	case 0x00: PPU.DispCnt = val; break;
	case 0x01: PPU_SetOBJCnt(val); break;
	
	case 0x02:
		PPU.OAMAddr = (PPU.OAMAddr & 0x100) | val;
		PPU.OAMReload = PPU.OAMAddr;
		PPU.FirstOBJ = PPU.OAMPrio ? ((PPU.OAMAddr >> 1) & 0x7F) : 0;
		break;
	case 0x03:
		PPU.OAMAddr = (PPU.OAMAddr & 0xFF) | ((val & 0x01) << 8);
		PPU.OAMPrio = val & 0x80;
		PPU.OAMReload = PPU.OAMAddr;
		PPU.FirstOBJ = PPU.OAMPrio ? ((PPU.OAMAddr >> 1) & 0x7F) : 0;
		break;
		
	case 0x04:
		if (PPU.OAMAddr & 0x200)
		{
			if (PPU.OAMAddr & 0x100)
				PPU.OAM[PPU.OAMAddr & 0x10F] = (PPU.OAM[PPU.OAMAddr & 0x10F] & 0x00FF) | (val << 8);
			else
				PPU.OAM[PPU.OAMAddr & 0xFF] = PPU.OAMVal | (val << 8);
			
			PPU.OAMAddr++;
			PPU.OAMAddr &= 0x1FF;
		}
		else
		{
			if (PPU.OAMAddr & 0x100)
				PPU.OAM[PPU.OAMAddr & 0x10F] = (PPU.OAM[PPU.OAMAddr & 0x10F] & 0xFF00) | val;
			else
				PPU.OAMVal = val;
			
			PPU.OAMAddr |= 0x200;
		}
		break;
	
	case 0x05: PPU.BGMode = val; break;
	case 0x06: PPU.BGMosaic = val; break;
		
	case 0x07: PPU_SetBGScr(0, val); break;
	case 0x08: PPU_SetBGScr(1, val); break;
	case 0x09: PPU_SetBGScr(2, val); break;
	case 0x0A: PPU_SetBGScr(3, val); break;
	
	case 0x0B:
		PPU.BGChr[0] = (val & 0x07) << 12;
		PPU.BGChr[1] = (val & 0x70) << 8;
		break;
	case 0x0C:
		PPU.BGChr[2] = (val & 0x07) << 12;
		PPU.BGChr[3] = (val & 0x70) << 8;
		break;
		
	case 0x0D: 
		PPU_SetBGXPos(0, val);
		PPU.M7XPos = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		break;
	case 0x0E: 
		PPU_SetBGYPos(0, val); 
		PPU.M7YPos = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		break;
	case 0x0F: PPU_SetBGXPos(1, val); break;
	case 0x10: PPU_SetBGYPos(1, val); break;
	case 0x11: PPU_SetBGXPos(2, val); break;
	case 0x12: PPU_SetBGYPos(2, val); break;
	case 0x13: PPU_SetBGXPos(3, val); break;
	case 0x14: PPU_SetBGYPos(3, val); break;
		
	case 0x15:
		PPU.VRAMInc = val;
		switch (val & 0x03)
		{
			case 0x00: PPU.VRAMStep = 1; break;
			case 0x01: PPU.VRAMStep = 32; break;
			case 0x02:
			case 0x03: PPU.VRAMStep = 128; break;
		}
		break;
		
	case 0x16:
		PPU.VRAMAddr &= 0x7F00;
		PPU.VRAMAddr |= val;
		addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
		PPU.VRAMPref = PPU_VRAM(addr);
		break;
	case 0x17:
		PPU.VRAMAddr &= 0x00FF;
		PPU.VRAMAddr |= ((val & 0x7F) << 8);
		addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
		PPU.VRAMPref = PPU_VRAM(addr);
		break;
	
	case 0x18:
		{
			addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
			PPU_VRAM(addr) = (PPU_VRAM(addr) & 0xFF00) | val;
			if (!(PPU.VRAMInc & 0x80))
				PPU.VRAMAddr += PPU.VRAMStep;
		}
		break;
	case 0x19:
		{
			addr = PPU_TranslateVRAMAddress(PPU.VRAMAddr);
			PPU_VRAM(addr) = (PPU_VRAM(addr) & 0x00FF) | (val << 8);
			if (PPU.VRAMInc & 0x80)
				PPU.VRAMAddr += PPU.VRAMStep;
		}
		break;
		
	case 0x1A: 
		PPU.M7Mode = val;
		break;
	case 0x1B:
		PPU.M7A = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		break;
	case 0x1C:
		PPU.M7B = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		break;
	case 0x1D:
		PPU.M7C = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		break;
	case 0x1E:
		PPU.M7D = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		break;
	case 0x1F:
		PPU.M7XCenter = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		break;
	case 0x20:
		PPU.M7YCenter = (s16)((val << 8) | PPU.M7Old);
		PPU.M7Old = val;
		break;
		
	case 0x21:
		PPU.CGRAMAddr = val;
		break;
	
	case 0x22:
		if (!(PPU.CGRAMAddr & 0x100))
		{
			PPU.CGRAMVal = val;
			PPU.CGRAMAddr |= 0x100;
		}
		else
		{
			PPU.CGRAM[PPU.CGRAMAddr & 0xFF] = PPU.CGRAMVal | (val << 8);
			PPU.CGRAMAddr++;
			PPU.CGRAMAddr &= 0xFF;
		}
		break;
		
	case 0x23:
		PPU.BGWindowMask[0] = val & 0xF;
		PPU.BGWindowMask[1] = val >> 4;
		PPU.WindowDirty = 1;
		break;
	case 0x24:
		PPU.BGWindowMask[2] = val & 0xF;
		PPU.BGWindowMask[3] = val >> 4;
		PPU.WindowDirty = 1;
		break;
	case 0x25:
		PPU.OBJWindowMask = val & 0xF;
		PPU.ColorMathWindowMask = val >> 4;
		PPU.WindowDirty = 1;
		break;
		
	case 0x26:
		PPU.WindowX[0] = val;
		PPU.WindowDirty = 1;
		break;
	case 0x27:
		PPU.WindowX[1] = val;
		PPU.WindowDirty = 1;
		break;
	case 0x28:
		PPU.WindowX[2] = val;
		PPU.WindowDirty = 1;
		break;
	case 0x29:
		PPU.WindowX[3] = val;
		PPU.WindowDirty = 1;
		break;
		
	case 0x2A:
		PPU.BGWindowCombine[0] = val & 0x3;
		PPU.BGWindowCombine[1] = (val >> 2) & 0x3;
		PPU.BGWindowCombine[2] = (val >> 4) & 0x3;
		PPU.BGWindowCombine[3] = (val >> 6) & 0x3;
		PPU.WindowDirty = 1;
		break;
	case 0x2B:
		PPU.OBJWindowCombine = val & 0x3;
		PPU.ColorMathWindowCombine = (val >> 2) & 0x3;
		PPU.WindowDirty = 1;
		break;
		
	case 0x2C: PPU.BGEnable = (PPU.BGEnable & 0xFF00) | val; break;
	case 0x2D: PPU.BGEnable = (PPU.BGEnable & 0x00FF) | (val << 8); break;
	
	case 0x2E: PPU.WindowEnable = (PPU.WindowEnable & 0xFF00) | val; break;
	case 0x2F: PPU.WindowEnable = (PPU.WindowEnable & 0x00FF) | (val << 8); break;
	
	case 0x30: PPU.ColorMath = (PPU.ColorMath & 0xFF00) | val; break;
	case 0x31: PPU.ColorMath = (PPU.ColorMath & 0x00FF) | (val << 8); break;
	
	case 0x32:
		addr = val & 0x1F;
		if (val & 0x20) PPU.SubBackdrop = (PPU.SubBackdrop & 0x7FE0) | addr;
		if (val & 0x40) PPU.SubBackdrop = (PPU.SubBackdrop & 0x7C1F) | (addr << 5);
		if (val & 0x80) PPU.SubBackdrop = (PPU.SubBackdrop & 0x03FF) | (addr << 10);
		break;
	
	case 0x33: PPU.SetIni = val; break;
	}
}


void PPU_DrawBG_2bpp_8x8(u16 num, u16* dst, u16 ypos)
{
	u16 tileset = PPU.BGChr[num] << 12;
	
	ypos += PPU.BGYPos[num];
	
	u16 bgscr = PPU.BGScr[num];
	u16 tilemap = ((bgscr & 0xFC) << 8) + ((ypos & 0xF8) << 2);
	if ((ypos & 0x100) && (bgscr & 0x2))
		tilemap += (bgscr & 0x1) ? 2048 : 1024;
	
	u16 xpos = PPU.BGXPos[num];
	u16 tmaddr = (xpos & 0xF8) >> 3;
	if ((xpos & 0x100) && (bgscr & 0x1))
		tmaddr += 1024;
	
	// preload first tile
	u16 curtile = PPU_VRAM(tilemap + tmaddr);
	u16* pal = &PPU.CGRAM[(curtile & 0x1C00) >> 8];
	
	u16 tsaddr = (curtile & 0x03FF) << 3;
	if (curtile & 0x8000) tsaddr += (7 - (ypos & 0x7));
	else                  tsaddr += (ypos & 0x7);
	
	u16 tiledata = PPU_VRAM(tileset + tsaddr);
	if (curtile & 0x4000) tiledata >>= (xpos & 0x7);
	else                  tiledata <<= (xpos & 0x7);
	
	for (u16 dstx = 0; dstx < 256; dstx++)
	{
		/*if (curtile & 0x4000)
		{
			u16 color = 0;
			if (tiledata & 0x0001) color |= 0x1;
			if (tiledata & 0x0100) color |= 0x2;
			
			if (color)
				dst[dstx] = pal[color] | 0x8000;

			tiledata >>= 1;
			xpos++;
		}
		else*/
		{
			u16 color = 0;
			if (tiledata & 0x0080) color |= 0x1;
			if (tiledata & 0x8000) color |= 0x2;
			//color |= ((tiledata & 0x0080) >> 7) | ((tiledata & 0x8000) >> 14);
			
			if (color)
				dst[dstx] = 0x83E0;//pal[color] | 0x8000;
			
			tiledata <<= 1;
			xpos++;
		}
		
		if (!(xpos & 0x7))
		{
			// load next tile
			tmaddr = (xpos & 0xF8) >> 3;
			if ((xpos & 0x100) && (bgscr & 0x1))
				tmaddr += 1024;
				
			curtile = PPU_VRAM(tilemap + tmaddr);
			pal = &PPU.CGRAM[(curtile & 0x1C00) >> 8];
		
			tsaddr = (curtile & 0x03FF) << 3;
			if (curtile & 0x8000) tsaddr += (7 - (ypos & 0x7));
			else                  tsaddr += (ypos & 0x7);
			
			tiledata = PPU_VRAM(tileset + tsaddr);
		}
	}
}

void PPU_DrawBG_4bpp_8x8(u16 num, u16* dst, u16 ypos)
{
	u16 tileset = PPU.BGChr[num] << 12;
	
	ypos += PPU.BGYPos[num];
	
	u16 bgscr = PPU.BGScr[num];
	u16 tilemap = ((bgscr & 0xFC) << 8) + ((ypos & 0xF8) << 2);
	if ((ypos & 0x100) && (bgscr & 0x2))
		tilemap += (bgscr & 0x1) ? 2048 : 1024;
	
	u16 xpos = PPU.BGXPos[num];
	u16 tmaddr = (xpos & 0xF8) >> 3;
	if ((xpos & 0x100) && (bgscr & 0x1))
		tmaddr += 1024;
	
	// preload first tile
	u16 curtile = PPU_VRAM(tilemap + tmaddr);
	u16* pal = &PPU.CGRAM[(curtile & 0x1C00) >> 6];
	
	u16 tsaddr = (curtile & 0x03FF) << 4;
	if (curtile & 0x8000) tsaddr += (7 - (ypos & 0x7));
	else                  tsaddr += (ypos & 0x7);
	
	u16 tiledata1 = PPU_VRAM(tileset + tsaddr);
	u16 tiledata2 = PPU_VRAM(tileset + tsaddr + 8);
	if (curtile & 0x4000) { tiledata1 >>= (xpos & 0x7); tiledata2 >>= (xpos & 0x7); }
	else                  { tiledata1 <<= (xpos & 0x7); tiledata2 <<= (xpos & 0x7); }
	
	for (u16 dstx = 0; dstx < 256; dstx++)
	{
		if (curtile & 0x4000)
		{
			//u16 color = 0;
			/*if (tiledata1 & 0x0001) color |= 0x1;
			if (tiledata1 & 0x0100) color |= 0x2;
			if (tiledata2 & 0x0001) color |= 0x4;
			if (tiledata2 & 0x0100) color |= 0x8;*/
			/*color |= (tiledata1 & 0x0001) | ((tiledata1 & 0x0100) >> 7);
			color |= ((tiledata2 & 0x0001) << 2) | ((tiledata2 & 0x0100) >> 5);*/
			u16 color = ((tiledata2 & 0x0101) + 0x007F) >> 7;
			color = (color << 2) + (((tiledata1 & 0x0101) + 0x007F) >> 7);
			
			if (color)
				dst[dstx] = pal[color] | 0x8000;

			tiledata1 >>= 1; tiledata2 >>= 1;
			xpos++;
		}
		else
		{
			//u16 color = 0;
			/*if (tiledata1 & 0x0080) color |= 0x1;
			if (tiledata1 & 0x8000) color |= 0x2;
			if (tiledata2 & 0x0080) color |= 0x4;
			if (tiledata2 & 0x8000) color |= 0x8;*/
			/*color |= ((tiledata1 & 0x0080) >> 7) | ((tiledata1 & 0x8000) >> 14);
			color |= ((tiledata2 & 0x0080) >> 5) | ((tiledata2 & 0x8000) >> 12);*/
			u16 color = ((tiledata2 & 0x8080) + 0x3F80) >> 14;
			color = (color << 2) + (((tiledata1 & 0x8080) + 0x3F80) >> 14);
			
			if (color)
				dst[dstx] = pal[color] | 0x8000;
			
			tiledata1 <<= 1; tiledata2 <<= 1;
			xpos++;
		}
		
		if (!(xpos & 0x7))
		{
			// load next tile
			tmaddr = (xpos & 0xF8) >> 3;
			if ((xpos & 0x100) && (bgscr & 0x1))
				tmaddr += 1024;
				
			curtile = PPU_VRAM(tilemap + tmaddr);
			pal = &PPU.CGRAM[(curtile & 0x1C00) >> 6];
		
			tsaddr = (curtile & 0x03FF) << 4;
			if (curtile & 0x8000) tsaddr += (7 - (ypos & 0x7));
			else                  tsaddr += (ypos & 0x7);
			
			tiledata1 = PPU_VRAM(tileset + tsaddr);
			tiledata2 = PPU_VRAM(tileset + tsaddr + 8);
		}
	}
}


void PPU_DrawScanline(u16 line)
{
	// TEST
	
	for (u16 i = PPU_CurOutput; i < PPU_CurOutput+256; i++)
		PPU_Output[i] = 0x8000;
	
	/*u16 addr = 0x4000;
	addr += (line & 0x7);
	addr += (line >> 3) << 8;
	
	for (u16 x = 0; x < 256;)
	{
		u16 tiledata = PPU_VRAM(addr);
		u16 tx = 0;
		for (; tx < 8; tx++)
		{
			u16 color = 0;
			if (tiledata & 0x0080) color |= 0x1;
			if (tiledata & 0x8000) color |= 0x2;
			
			if (color)
				PPU_Output[x] = PPU.CGRAM[color] | 0x8000;
			
			tiledata <<= 1;
			x++;
		}
		
		addr += 8;
	}*/
	
	timer_start();
	u32 t1 = timer_read();
	
	/*for (u16 i = PPU_CurOutput; i < PPU_CurOutput+256; i++)
		PPU_Output[i] = 0x83E0;
	for (u16 i = PPU_CurOutput; i < PPU_CurOutput+256; i++)
		PPU_Output[i] = 0xFC00;*/
	//PPU_DrawBG_4bpp_8x8(0, &PPU_Output[PPU_CurOutput], line);
	//for (u16 i = PPU_CurOutput; i < PPU_CurOutput+256; i+=2)
	//	*(u32*)&PPU_Output[i] = 0x83E083E0;
	//PPU_test(&PPU_Output[PPU_CurOutput]);
	//PPU_test2(&PPU_Output[PPU_CurOutput+1]);
	PPU_DoDrawScanline(&PPU, &PPU_Output[PPU_CurOutput], line);
	
	u32 t2 = timer_read();
	u32 diff = t1-t2;
	PPU_Output[PPU_CurOutput] = diff & 0xFFFF;
	PPU_Output[PPU_CurOutput+1] = diff >> 16;
	
	//PPU_DrawBG_2bpp_8x8(2, PPU_Output, line);
	//PPU_DrawBG_4bpp_8x8(0, &PPU_Output[PPU_CurOutput], line);
	
	//or (u16 i = 0; i < 256; i++) PPU_Output[i] = PPU.CGRAM[i]|0x8000;
	dma_waitBusy();
	//dma_transferDspToArm9(&PPU_Output[PPU_CurOutput], 0x06800000+(line<<9), 256);
	dma_transferDspToArm9(&PPU_Output[PPU_CurOutput], 0x06000000+(line<<9), 256);
	//dma_transferDspToArm9(&PPU_VRAM(PPU_CurOutput), 0x06000000+(line<<9), 256);
	PPU_CurOutput ^= 256;
	//PPU_CurOutput += 0x100;
	//PPU_CurOutput &= 0x300;
}

void PPU_VBlank(void)
{
	PPU.OAMAddr = PPU.OAMReload;
}
