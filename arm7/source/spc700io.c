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
	// 0x00
	u8 EnableMask;
	u8 __fgqgqdfh;
	
	// timer 0: 0x02
	// timer 1: 0x08
	// timer 2: 0x0E
	struct
	{
		u16 CycleCount;
		u16 Reload;
		u8 Val;
		u8 __zerfzergdf;
		
	} Timer[3];
	
} SPC_Timers;

u8 SPC_ROMAccess;
u8 SPC_DSPAddr;

bool speedhaxed = false;


void SPC_InitMisc()
{
	SPC_ROMAccess = 1;
	SPC_DSPAddr = 0;
	
	SPC_Timers.EnableMask = 0;
	SPC_Timers.Timer[0].CycleCount = 0;
	SPC_Timers.Timer[0].Reload = 0;
	SPC_Timers.Timer[0].Val = 0;
	SPC_Timers.Timer[1].CycleCount = 0;
	SPC_Timers.Timer[1].Reload = 0;
	SPC_Timers.Timer[1].Val = 0;
	SPC_Timers.Timer[2].CycleCount = 0;
	SPC_Timers.Timer[2].Reload = 0;
	SPC_Timers.Timer[2].Val = 0;
	
	DSP_Reset();
	
	speedhaxed = false;
}

u8 SPC_IORead8(u16 addr)
{
	asm("stmdb sp!, {r1-r3, r12}");
	
	u8 ret = 0;
	switch (addr)
	{
		case 0xF2: ret = SPC_DSPAddr; break;
		case 0xF3: ret = DSP_Read(SPC_DSPAddr); break;
		
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
				else { SPC_Timers.Timer[0].CycleCount = SPC_Timers.Timer[0].Reload; }
				if (!(val & 0x02)) SPC_Timers.Timer[1].Val = 0;
				else { SPC_Timers.Timer[1].CycleCount = SPC_Timers.Timer[1].Reload; }
				if (!(val & 0x04)) SPC_Timers.Timer[2].Val = 0;
				else { SPC_Timers.Timer[2].CycleCount = SPC_Timers.Timer[2].Reload; }
				
				if (val & 0x10) *(u16*)&IPC->SPC_IOPorts[0] = 0x0000;
				if (val & 0x20) *(u16*)&IPC->SPC_IOPorts[2] = 0x0000;
				
				SPC_ROMAccess = (val & 0x80) ? 1:0;
			}
			break;
			
		case 0xF2: SPC_DSPAddr = val; break;
		case 0xF3: DSP_Write(SPC_DSPAddr, val); break;
			
		case 0xF4: IPC->SPC_IOPorts[4] = val; break;
		case 0xF5: IPC->SPC_IOPorts[5] = val; break;
		case 0xF6: IPC->SPC_IOPorts[6] = val; break;
		case 0xF7: IPC->SPC_IOPorts[7] = val; break;
		
		case 0xFA: SPC_Timers.Timer[0].Reload = val << 7; break;
		case 0xFB: SPC_Timers.Timer[1].Reload = val << 7; break;
		case 0xFC: SPC_Timers.Timer[2].Reload = val << 4; break;
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


void SPC_ApplySpeedHacks()
{
	if (speedhaxed) return;
	speedhaxed = true;
	return;
	int i;
	for (i = 0; i < 0xFFC0;)
	{
		if (SPC_RAM[i] == 0xEC && SPC_RAM[i+1] >= 0xFD && SPC_RAM[i+1] <= 0xFE && SPC_RAM[i+2] == 0x00
			&& (SPC_RAM[i+3] & 0x1F) == 0x10 && SPC_RAM[i+4] == 0xFB)
		{
			u8 offset = SPC_RAM[i+4] & 0x0F;
			u8 branchtype = SPC_RAM[i+3] & 0xF0;
			
			SPC_RAM[i+3] = 0xFF;
			SPC_RAM[i+4] = branchtype | offset;
			i += 5;
			continue;
		}
		
		i++;
	}
}
