.section .text

#define PPU_BGScr		0x10s7
#define PPU_BGChr		0x14s7
#define PPU_BGXScr2		0x18s7
#define PPU_BGYScr2		0x1Cs7
#define PPU_BGXPos		0x20s7
#define PPU_BGYPos 		0x24s7

.global PPU_DoDrawScanline


// a0l = BG#
// b0l = Y pos.
// b1l = output buffer??????
// r7 = PPU state struct.
DrawBG_8x8_4bpp:
	push r0
	push r1
	push r2
	push r3
	push r4
	push r5
	push r6
	push r7
	
	mov r7, ext3
	addv 0x60, ext3 	// ext3 = CGRAM
	addl r7, a0
	mov a0l, r7			// r7 = PPU struct + BG# (for accessing BG state)
	
	mov 0xC, a0h
	or 0x3, a0
	mov a0, b1
	
	mov b0, a0
	add [r7+PPU_BGYPos], a0	// a0l = Y pos. + Y scroll
	mov a0l, r4
	and 0x7, a0
	mov a0l, ext1		// ext1 = ypos & 7
	//.short 0xD4BC, 0x8801
	mov r4, a0l
	
	// calculate base screen address for this scanline
	
	mov [r7+PPU_BGScr], a1		// PPU.BGScr[n]
	and 0xF8, a0
	shfi a0, a0, +2
	add a0, a1
	tst0 0x100, r4
	brr _draw884_no_y512, eq
		add [r7+PPU_BGYScr2], a1
_draw884_no_y512:
	mov a1l, ext2		// ext2 = base screen addr.
	//.short 0xD5BC, 0x8800
	
	// load first tile
	
	clr a0, always
	
_draw884_main_loop:
	mov a0, r6				// r6 = current X pos.
	
	// load tile
	
	mov [r7+PPU_BGXPos], a1		// PPU.BGXPos[n]
	add r6, a1
	mov a1l, r3
	and 0xF8, a1
	shfi a1, a1, -3
	tst0 0x100, r3
	brr _draw884_no_x512, eq
		add [r7+PPU_BGXScr2], a1
_draw884_no_x512:
	add ext2, a1
	//.short 0xD5F8, 0x8800
	mov a1l, r2
	mov [r2], r3	// r3 = tilemap entry
	
	mov ext3, r2
	mov r3, a1l
	and 0x1C00, a1
	shfi a1, a1, -6
	add r2, a1
	mov a1l, r2		// r2 = tile palette
	
	// load tile data
	
	mov r3, a1l
	and 0x03FF, a1
	shfi a1, a1, +4
	mov ext1, a0
	//.short 0xD4B8, 0x8801
	tst0 0x8000, r3 // yflip
	brr _draw884_no_yflip, eq
		xor 7u8, a0
_draw884_no_yflip:
	add a0, a1
	mov [r7+PPU_BGChr], a0	// PPU.BGChr[n]
	add a0, a1
	mov a1l, r4		// r4 = offset to tile data
	mov [r4], a0l	// r0 = tile planes 0/1
	addv 8, r4
	mov [r4], a1l	// r1 = tile planes 2/3
	
	tst0 0x4000, r3
	brr _draw884_xflip, neq
		//movp a0l, r0
		.short 0x0040
		//movp a1l, r1
		.short 0x0061
		brr _draw884_no_xflip, always
_draw884_xflip:
		shfi a0, a0, +8
		shfi a1, a1, +8
		or a0h, a0
		or a1h, a1
		//movp a0l, r0
		.short 0x0040
		//movp a1l, r1
		.short 0x0061
		bitrev r0
		bitrev r1
_draw884_no_xflip:
	
	// draw tile
	
	mov 8, a1l
	mov [r7+PPU_BGXPos], a0		// PPU.BGXPos[n]
	add r6, a0
	and 7, a0
	sub a0, a1
	mov a1l, y0			// y0 = number of pixels to draw
	shl a0, always
	mov a0l, sv
	//mov r0, a0
	//mov r1, a0h
	mov r1, a0l
	shfi a0, a0, +18
	or r0, a0
	//shfc a0, a0, always	// FIXME goes the wrong way -- need neg opcode
	mov a0, b0
	
	// adjust y0 if we are drawing at the right edge
	cmpv 248, r6
	brr _draw844_notlasttile, le
	mov 256, a0l
	sub r6, a0
	mov a0l, y0
_draw844_notlasttile:
	
	subv 1, y0
	mov ext0, r5
	
	bkrep y0, _draw884_tile_loop
		and b0, b1, a0
		or a0h, a0

		brr _draw884_transparent, eq
			add r2, a0
			mov a0l, r4
			mov [r4], r4
			set 0x8000, r4		// TODO
			mov r4, [r5]		// store final pixel
_draw884_transparent:

		modr [r5++]
		shfi b0, b0, -2
_draw884_tile_loop:

	mov r5, ext0
	
	mov r6, a0
	add y0, a0
	add 1u8, a0
	cmp 255u8, a0
	br _draw884_main_loop, le
	
	pop r7
	pop r6
	pop r5
	pop r4
	pop r3
	pop r2
	pop r1
	pop r0
	ret always


// a0l = PPU state struct.
// a1l = output buffer
// b0l = Y pos.
PPU_DoDrawScanline:
	push r0
	push r1
	push r2
	push r7
	set 0x80, mod0
	load 0x1, movpd
	
	mov a1l, r0
	mov b0l, r1
	mov a0l, r7
	
	// if forced blank enabled: fill screen black
	tst0 0x80, [r7]	// DispCnt
	brr _draw_notforcedblank, eq
	mov 0x8000, a0h
	or 0x8000, a0
	mov 0x7F, r1
	bkrep r1, _draw_blankloop
	mov a0, [arrn0+ars1] // store to [r0], postincrement of 2
	nop
_draw_blankloop:
	br _draw_ret, always
	
_draw_notforcedblank:
	/*mov a1l, r0
	mov a0l, r2
	addv 0x60, r2
	mov 0x100, r1
	bkrep r1, vorpi
	mov [r2++], a0l
	or 0x8000, a0
	mov a0l, [r0++]
vorpi:*/

	push r0
	mov 0x8000, a0h
	or 0x8000, a0
	//mov 0, a0
	mov 0x7F, r1
	bkrep r1, _draw_backdroploop
	mov a0, [arrn0+ars1] // store to [r0], postincrement of 2
	nop
_draw_backdroploop:
	pop r0

	mov 0, a0l
	// TODO save/restore b0l
	//mov r0, b1l
	mov r0, ext0
	call DrawBG_8x8_4bpp, always
	
_draw_ret:
	rst 0x80, mod0
	pop r7
	pop r2
	pop r1
	pop r0
	ret always




.global PPU_test
.global PPU_test2
PPU_test:
	push r0
	push r1
	
	mov a0l, r0
	mov 0x83E0, a0l
	mov 0x100, r1
	bkrep r1, vorp
	mov a0l, [r0++]
vorp:
	
	pop r1
	pop r0
	ret always
	
	
PPU_test2:
	push r0
	push r1
	
	mov a0l, r0
	mov 0xFC00, a0h
	mov 0x83E0, a0
	mov 0x80, r1
	bkrep r1, vorp2
	mov a0, [arrn0+ars1]
vorp2:
	
	pop r1
	pop r0
	ret always
