
#ifndef COMMON_H
#define COMMON_H

typedef struct
{
	// SPC700 IO
	u8 SPC_IOPorts[8];
	
	u8 Input_XY;
	
	// debug section
	char Dbg_String[256];
	
} IPCStruct;

extern IPCStruct* IPC;

#endif
