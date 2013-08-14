#include <nds.h>

extern u8* SPC_IOPorts;
void Sync_RunSPC()
{
	asm("stmdb sp!, {r12}");
	
	*(u16*)0x04000180 = 0x6100;
	//if (!((*(u16*)0x04000130) & 0x0001))
	//	iprintf("SPC PC: %02X%02X\n", SPC_IOPorts[9], SPC_IOPorts[8]);
	
	asm("ldmia sp!, {r12}");
}
