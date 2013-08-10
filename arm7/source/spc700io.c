#include <nds.h>

#include "spc700.h"


u8* SPC_IOPorts;

struct SPC_TimersStruct
{
	u8 EnableMask;
	struct
	{
		u8 CycleCount;
		u8 IterCount;
		u8 Limit;
		u8 Val;
		
	} Timer[3];
	
} SPC_Timers;

u8 SPC_ROMAccess;


void SPC_InitMisc()
{
	SPC_ROMAccess = 1;
	
	SPC_Timers.EnableMask = 0;
	SPC_Timers.Timer[0].CycleCount = 128;
	SPC_Timers.Timer[0].IterCount = 0;
	SPC_Timers.Timer[0].Limit = 0;
	SPC_Timers.Timer[0].Val = 0;
	SPC_Timers.Timer[1].CycleCount = 128;
	SPC_Timers.Timer[1].IterCount = 0;
	SPC_Timers.Timer[1].Limit = 0;
	SPC_Timers.Timer[1].Val = 0;
	SPC_Timers.Timer[2].CycleCount = 16;
	SPC_Timers.Timer[2].IterCount = 0;
	SPC_Timers.Timer[2].Limit = 0;
	SPC_Timers.Timer[2].Val = 0;
}

u8 SPC_IORead8(u16 addr)
{
	u8 ret = 0;
	switch (addr)
	{
		case 0xF4: ret = SPC_IOPorts[0]; break;
		case 0xF5: ret = SPC_IOPorts[1]; break;
		case 0xF6: ret = SPC_IOPorts[2]; break;
		case 0xF7: ret = SPC_IOPorts[3]; break;
		
		case 0xFD: ret = SPC_Timers.Timer[0].Val; SPC_Timers.Timer[0].Val = 0; break;
		case 0xFE: ret = SPC_Timers.Timer[1].Val; SPC_Timers.Timer[1].Val = 0; break;
		case 0xFF: ret = SPC_Timers.Timer[2].Val; SPC_Timers.Timer[2].Val = 0; break;
	}
	
	return ret;
}

u16 SPC_IORead16(u16 addr)
{
	u16 ret = 0;
	switch (addr)
	{
		case 0xF4: ret = *(u16*)&SPC_IOPorts[0]; break;
		case 0xF6: ret = *(u16*)&SPC_IOPorts[2]; break;
		
		default:
			ret = SPC_IORead8(addr);
			ret |= ((u16)SPC_IORead8(addr+1) << 8);
			break;
	}
	
	return ret;
}

void SPC_IOWrite8(u16 addr, u8 val)
{
	switch (addr)
	{
		case 0xF1:
			{
				SPC_Timers.EnableMask = val & 0x07;
				
				if (!(val & 0x01)) SPC_Timers.Timer[0].Val = 0;
				else { SPC_Timers.Timer[0].CycleCount = 128; SPC_Timers.Timer[0].IterCount = 0; }
				if (!(val & 0x02)) SPC_Timers.Timer[1].Val = 0;
				else { SPC_Timers.Timer[1].CycleCount = 128; SPC_Timers.Timer[1].IterCount = 0; }
				if (!(val & 0x04)) SPC_Timers.Timer[2].Val = 0;
				else { SPC_Timers.Timer[2].CycleCount = 16; SPC_Timers.Timer[2].IterCount = 0; }
				
				if (val & 0x10) *(u16*)&SPC_IOPorts[0] = 0x0000;
				if (val & 0x20) *(u16*)&SPC_IOPorts[2] = 0x0000;
				
				SPC_ROMAccess = (val & 0x80) ? 1:0;
			}
			break;
			
		case 0xF4: SPC_IOPorts[4] = val; break;
		case 0xF5: SPC_IOPorts[5] = val; break;
		case 0xF6: SPC_IOPorts[6] = val; break;
		case 0xF7: SPC_IOPorts[7] = val; break;
		
		case 0xFA: SPC_Timers.Timer[0].Limit = val; break;
		case 0xFB: SPC_Timers.Timer[1].Limit = val; break;
		case 0xFC: SPC_Timers.Timer[2].Limit = val; break;
	}
}

void SPC_IOWrite16(u16 addr, u16 val)
{
	switch (addr)
	{
		case 0xF4: *(u16*)&SPC_IOPorts[4] = val; break;
		case 0xF6: *(u16*)&SPC_IOPorts[6] = val; break;
		
		default:
			SPC_IOWrite8(addr, val & 0xFF);
			SPC_IOWrite8(addr+1, val >> 8);
			break;
	}
}


// debug
#include <stdio.h>
/*__attribute__((section(".ewram"), long_call)) void loldump()
{
	FILE* f = fopen("spcram.bin", "wb");
	int i;
	u32 val;
	
	for (i = 0; i < 0x10000; i += 4)
	{
		val = *(u32*)&SPC_Memory[i];
		fwrite(&val, 4, 1, f);
	}
	
	fflush(f);
	fclose(f);
}*/
