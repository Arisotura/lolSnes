
#ifndef COMMON_H
#define COMMON_H

typedef struct
{
	// SPC700 IO
	volatile u8 SPC_IOPorts[8];
	
	volatile u8 Input_XY;
	volatile u8 Pause;
	
	// debug section
	volatile char Dbg_String[256];
	
} IPCStruct;

extern IPCStruct* IPC;

#endif
