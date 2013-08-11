#include <nds.h>

#include "memory.h"


u8 DMA_Chans[8*16];


u8 DMA_Read8(u32 addr)
{
	asm("stmdb sp!, {r12}");
	
	if (addr > 0x7F)
	{
		asm("ldmia sp!, {r12}");
		return 0;
	}
	
	register u8 ret = DMA_Chans[addr];
	
	asm("ldmia sp!, {r12}");
	return ret;
}

u16 DMA_Read16(u32 addr)
{
	asm("stmdb sp!, {r12}");
	
	if (addr > 0x7F)
	{
		asm("ldmia sp!, {r12}");
		return 0;
	}
	
	u16 ret = DMA_Chans[addr] | (DMA_Chans[addr+1] << 8);

	asm("ldmia sp!, {r12}");
	return ret;
}

void DMA_Write8(u32 addr, u8 val)
{
	asm("stmdb sp!, {r12}");

	if (addr > 0x7F)
	{
		asm("ldmia sp!, {r12}");
		return;
	}
	
	DMA_Chans[addr] = val;
	
	asm("ldmia sp!, {r12}");
}

void DMA_Write16(u32 addr, u16 val)
{
	asm("stmdb sp!, {r12}");
	
	if (addr > 0x7F)
	{
		asm("ldmia sp!, {r12}");
		return;
	}
	
	DMA_Chans[addr] = val & 0xFF;
	DMA_Chans[addr + 1] = val >> 8;
	
	asm("ldmia sp!, {r12}");
}

void DMA_Enable(u8 flag)
{
	int c;
	for (c = 0; c < 8; c++)
	{
		if (!(flag & (1 << c)))
			continue;
		
		u8* chan = &DMA_Chans[c << 4];
		u8 params = chan[0];
		
		u16 maddrinc;
		switch (params & 0x18)
		{
			case 0x00: maddrinc = 1; break;
			case 0x10: maddrinc = -1; break;
			default: maddrinc = 0; break;
		}
		
		u8 paddrinc = params & 0x07;
		
		u8 ppuaddr = chan[1];
		u16 memaddr = chan[2] | (chan[3] << 8);
		u32 membank = chan[4] << 16;
		u16 bytecount = chan[5] | (chan[6] << 8);
		//iprintf("DMA%d %d %06X %s 21%02X | m:%d p:%d\n", c, bytecount, memaddr|membank, (params&0x80)?"<-":"->", ppuaddr, maddrinc, paddrinc);
		if (params & 0x80)
		{
			for (;;)
			{
				switch (paddrinc)
				{
					case 0:
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--;
						break;
					case 1:
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
						memaddr += maddrinc; bytecount--;
						break;
					case 2:
					case 6:
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--;
						break;
					case 3:
					case 7:
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
						memaddr += maddrinc; bytecount--;
						break;
					case 4:
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr+2));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr+3));
						memaddr += maddrinc; bytecount--;
						break;
					case 5:
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						Mem_Write8(membank|memaddr, PPU_Read8(ppuaddr+1));
						memaddr += maddrinc; bytecount--;
						break;
				}
				
				if (!bytecount) break;
			}
		}
		else
		{
			for (;;)
			{
				switch (paddrinc)
				{
					case 0:
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--;
						break;
					case 1:
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr+1, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--;
						break;
					case 2:
					case 6:
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--;
						break;
					case 3:
					case 7:
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr+1, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr+1, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--;
						break;
					case 4:
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr+1, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr+2, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr+3, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--;
						break;
					case 5:
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr+1, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--; if (!bytecount) break;
						PPU_Write8(ppuaddr+1, Mem_Read8(membank|memaddr));
						memaddr += maddrinc; bytecount--;
						break;
				}
				
				if (!bytecount) break;
			}
		}
		
		chan[2] = memaddr & 0xFF;
		chan[3] = memaddr >> 8;
		
		chan[5] = 0;
		chan[6] = 0;
	}
}
