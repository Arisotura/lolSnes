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

#include "spc700.h"


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
	asm("stmdb sp!, {r1-r3, r12}");
	
	u8 ret = 0;
	switch (addr)
	{
		case 0xF4: ret = IPC->SPC_IOPorts[0]; break;
		case 0xF5: ret = IPC->SPC_IOPorts[1]; break;
		case 0xF6: ret = IPC->SPC_IOPorts[2]; break;
		case 0xF7: ret = IPC->SPC_IOPorts[3]; break;
		
		case 0xFD: ret = SPC_Timers.Timer[0].Val; SPC_Timers.Timer[0].Val = 0; break;
		case 0xFE: ret = SPC_Timers.Timer[1].Val; SPC_Timers.Timer[1].Val = 0; break;
		case 0xFF: ret = SPC_Timers.Timer[2].Val; SPC_Timers.Timer[2].Val = 0; break;
	}
	
	asm("ldmia sp!, {r1-r3, r12}");
	return ret;
}

u16 SPC_IORead16(u16 addr)
{
	asm("stmdb sp!, {r1-r3, r12}");
	
	u16 ret = 0;
	switch (addr)
	{
		case 0xF4: ret = *(u16*)&IPC->SPC_IOPorts[0]; break;
		case 0xF6: ret = *(u16*)&IPC->SPC_IOPorts[2]; break;
		
		default:
			ret = SPC_IORead8(addr);
			ret |= ((u16)SPC_IORead8(addr+1) << 8);
			break;
	}
	
	asm("ldmia sp!, {r1-r3, r12}");
	return ret;
}

void SPC_IOWrite8(u16 addr, u8 val)
{
	asm("stmdb sp!, {r1-r3, r12}");
	
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
				
				if (val & 0x10) *(u16*)&IPC->SPC_IOPorts[0] = 0x0000;
				if (val & 0x20) *(u16*)&IPC->SPC_IOPorts[2] = 0x0000;
				
				SPC_ROMAccess = (val & 0x80) ? 1:0;
			}
			break;
			
		case 0xF4: IPC->SPC_IOPorts[4] = val; break;
		case 0xF5: IPC->SPC_IOPorts[5] = val; break;
		case 0xF6: IPC->SPC_IOPorts[6] = val; break;
		case 0xF7: IPC->SPC_IOPorts[7] = val; break;
		
		case 0xFA: SPC_Timers.Timer[0].Limit = val; break;
		case 0xFB: SPC_Timers.Timer[1].Limit = val; break;
		case 0xFC: SPC_Timers.Timer[2].Limit = val; break;
	}
	
	asm("ldmia sp!, {r1-r3, r12}");
}

void SPC_IOWrite16(u16 addr, u16 val)
{
	asm("stmdb sp!, {r1-r3, r12}");
	
	switch (addr)
	{
		case 0xF4: *(u16*)&IPC->SPC_IOPorts[4] = val; break;
		case 0xF6: *(u16*)&IPC->SPC_IOPorts[6] = val; break;
		
		default:
			SPC_IOWrite8(addr, val & 0xFF);
			SPC_IOWrite8(addr+1, val >> 8);
			break;
	}
	
	asm("ldmia sp!, {r1-r3, r12}");
}
