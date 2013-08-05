#include <nds.h>

#include "spc700.h"


u8* SPC_IOPorts;


u8 SPC_IORead8(u16 addr)
{
	u8 ret = 0;
	switch (addr)
	{
		case 0xF4: ret = SPC_IOPorts[0]; break;
		case 0xF5: ret = SPC_IOPorts[1]; break;
		case 0xF6: ret = SPC_IOPorts[2]; break;
		case 0xF7: ret = SPC_IOPorts[3]; break;
	}
	
	return ret;
}

void SPC_IOWrite8(u16 addr, u8 val)
{
	switch (addr)
	{
		case 0xF4: SPC_IOPorts[4] = val; break;
		case 0xF5: SPC_IOPorts[5] = val; break;
		case 0xF6: SPC_IOPorts[6] = val; break;
		case 0xF7: SPC_IOPorts[7] = val; break;
	}
}
