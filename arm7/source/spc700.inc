
@ --- Register aliases --------------------------------------------------------

spcA 			.req r11
spcX			.req r10
spcY			.req r9
spcSP			.req r8
spcPC			.req r7		@ high hword: PC
spcPSW			.req r7		@ low hword: PSW
spcCycles		.req r6		@ SPC700 cycle count
opTable			.req r5		@ pointer to the opcode table in use
memory			.req r4		@ pointer to the memory

@ --- Variables and whatever --------------------------------------------------

.equ flagC, 0x01
.equ flagZ, 0x02
.equ flagI, 0x04	@ interrupt enable (unused)
.equ flagH, 0x08	@ half carry
.equ flagB, 0x10	@ break (unused)
.equ flagP, 0x20	@ direct page
.equ flagV, 0x40
.equ flagN, 0x80
.equ flagR, 0x100	@ ROM access flag (0 = RAM, 1 = ROM)

.equ flagNVHZC, 0xCB
.equ flagNVZC,  0xC3
.equ flagNZ,    0x82
.equ flagNZC,   0x83
.equ flagNVZ,   0xC2
.equ flagVH,    0x48
.equ flagNVH,   0xC8
.equ flagNVHZ,  0xCA
.equ flagZC,    0x03
