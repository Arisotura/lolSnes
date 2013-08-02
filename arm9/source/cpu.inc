
@ --- Register aliases --------------------------------------------------------

snesA 			.req r12
snesX			.req r11
snesY			.req r10
snesS			.req r9		@ high hword: S
snesPBR			.req r9		@ low hword: PBR
snesD			.req r8		@ high hword: D
snesDBR			.req r8		@ low hword: DBR
snesPC			.req r7		@ high hword: PC
snesP			.req r7		@ low hword: P
snesCycles		.req r6		@ high hword: master cycle count
snesLines		.req r6		@ low hword: scanline count (0-261)
opTable			.req r5		@ pointer to the opcode table in use
memoryMap		.req r4		@ pointer to the memory map table

@ --- Variables and whatever --------------------------------------------------

.equ flagC, 0x01
.equ flagZ, 0x02
.equ flagI, 0x04
.equ flagD, 0x08
.equ flagX, 0x10
.equ flagB, 0x10
.equ flagM, 0x20
.equ flagV, 0x40
.equ flagN, 0x80
.equ flagE, 0x100	@ not actually in P, but hey, we have to keep it somewhere
.equ flagW, 0x200	@ not even an actual flag... set by WAI
.equ flagI2, 0x400	@ not an actual flag either, set when NMIs are disabled

.equ flagNVZC, 0xC3
.equ flagNZ,   0x82
.equ flagNZC,  0x83
.equ flagNVZ,  0xC2

@ --- Debugging ---------------------------------------------------------------

.macro DbgPrint
	stmdb sp!, {r12, lr}
	bl iprintf
	ldmia sp!, {r12, lr}
.endm

.macro DbgPrintAndHalt stuff
	add r0, pc, #8
	bl iprintf
1:
	swi #0x50000
	b 1b
.ascii \stuff
.byte 0
.align 4
.endm
