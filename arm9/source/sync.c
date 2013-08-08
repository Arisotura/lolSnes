#include <nds.h>


void Sync_RunSPC()
{
	asm("stmdb sp!, {r12}");
	
	*(u16*)0x04000180 = 0x6100;
	
	asm("ldmia sp!, {r12}");
}
