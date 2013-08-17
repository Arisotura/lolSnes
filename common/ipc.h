
#ifndef COMMON_H
#define COMMON_H

typedef struct
{
	// SPC700 IO
	u8 SPC_IOPorts[8];
	u16 _debug;
	
	u8 Input_XY;
	
} IPCStruct;

extern IPCStruct* IPC;

#endif
