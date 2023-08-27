.section .text

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
	
	addl r7, a0
	mov a0l, r5
	
	mov 0xC, a0h
	or 0x3, a0
	mov a0, b1
	
	addv 0x24, r5
	mov b0, a0
	add [r5], a0	// a0l = Y pos. + Y scroll
	mov a0l, r4
	and 0x7, a0
	//mov a0l, [0x8801]		// [8801] = ypos & 7
	.short 0xD4BC, 0x8801
	mov r4, a0l
	
	// calculate base screen address for this scanline
	
	subv 0x14, r5
	mov [r5], a1l		// PPU.BGScr[n]
	and 0xF8, a0
	shfi a0, a0, +2
	add a0, a1
	tst0 0x100, r4
	brr _draw884_no_y512, eq
		addv 0xC, r5
		add [r5], a1
		subv 0xC, r5
_draw884_no_y512:
	//mov a1l, [0x8800]		// [8800] = base screen addr.
	.short 0xD5BC, 0x8800
	
	subv 0x10, r5
	
	mov 0, r6				// r6 = current X pos.
_draw884_main_loop:
	// load tile
	
	push r5
	addv 0x20, r5
	mov [r5], a1l		// PPU.BGXPos[n]
	add r6, a1
	mov a1l, r3
	and 0xF8, a1
	shfi a1, a1, -3
	tst0 0x100, r3
	brr _draw884_no_x512, eq
		subv 0x8, r5
		add [r5], a1
		addv 0x8, r5
_draw884_no_x512:
	//add [0x8800], a1
	.short 0xD5F8, 0x8800
	mov a1l, r2
	mov [r2], r3	// r3 = tilemap entry
	
	mov r7, r2
	addv 0x60, r2
	mov r3, a1l
	and 0x1C00, a1
	shfi a1, a1, -6
	add r2, a1
	mov a1l, r2		// r2 = tile palette
	
	// load tile data
	
	mov r3, a1l
	and 0x03FF, a1
	shfi a1, a1, +4
	//mov [0x8801], a0
	.short 0xD4B8, 0x8801
	tst0 0x8000, r3 // yflip
	brr _draw884_no_yflip, eq
		xor 7, a0
_draw884_no_yflip:
	add a0, a1
	subv 0xC, r5
	mov [r5], a0l	// PPU.BGChr[n]
	add a0, a1
	mov a1l, r4		// r4 = offset to tile data
	mov [r4], a0l	// r0 = tile planes 0/1
	//movp a0l, r0
	.short 0x0040
	addv 8, r4
	mov [r4], a0l	// r1 = tile planes 2/3
	//movp a0l, r1
	.short 0x0041
	
	// TODO: xflip
	
	// draw tile
	
	mov 8, a1l
	addv 0xC, r5
	mov [r5], a0l		// PPU.BGXPos[n]
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
	pusha a0
	push a0e
	mov r6, a0l
	cmp 248, a0
	brr _draw844_notlasttile, le
	mov 256, a0l
	sub r6, a0
	mov a0l, y0
_draw844_notlasttile:
	pop a0e
	popa a0
	
	subv 1, y0
	//mov b1l, r5
	.short 0x58B4 //mov ext0, r5
	
	//swap (a0,b0),(a1,b1)
	//.short 0x4984
	//mov 0x8080, r0
	//mov 0x3F80, r1
	bkrep y0, _draw884_tile_loop
		and b0, b1, a0
		or a0h, a0
		
		//tst0 0xF, a0l
		brr _draw884_transparent, eq
			add r2, a0
			mov a0l, r4
			mov [r4], r4
			set 0x8000, r4		// TODO
			mov r4, [r5]		// store final pixel
_draw884_transparent:

		addv 1, r5
		addv 1, r6
		//shl b0, always
		shfi b0, b0, -2
_draw884_tile_loop:
	//swap (a0,b0),(a1,b1)
	//.short 0x4984
	//mov r5, b1l
	.short 0x5A85 // mov r5, ext0
	pop r5
	
	cmpv 256, r6
	br _draw884_main_loop, lt
	
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
	.short 0x5A80 //mov r0, ext0
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
