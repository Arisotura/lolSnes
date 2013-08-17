
#ifndef _SPC700_H_
#define _SPC700_H_

#include "../../common/ipc.h"

typedef union
{
	u16 val;
	struct
	{
		u16 C :1,
			Z :1,
			I :1,
			D :1,
			X :1,	// B in emulation mode
			M :1,
			V :1,
			N :1,
			E :1,	// actually not in P, but hey, we have to keep it somewhere
			W :1,	// not even an actual flag, set by WAI
			I2 :1,	// not an actual flag either, set when NMI's are disabled
			  :5;
	};
} SPC_PSW;

typedef struct
{
	u32 _memoryMap;
	u32 _opTable;
	s32 nCycles;
	SPC_PSW PSW;
	u16 PC;
	u32 SP;
	u32 Y;
	u32 X;
	u32 A;
} SPC_Regs_t;

extern SPC_Regs_t SPC_Regs;

extern u8 SPC_ROM[0x40];

extern struct SPC_TimersStruct SPC_Timers;

	
void SPC_Reset();
void SPC_Run();

void SPC_InitMisc();

u8 SPC_IORead8(u16 addr);
u16 SPC_IORead16(u16 addr);
void SPC_IOWrite8(u16 addr, u8 val);
void SPC_IOWrite16(u16 addr, u16 val);

#endif
