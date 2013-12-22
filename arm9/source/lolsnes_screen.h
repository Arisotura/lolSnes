
//{{BLOCK(lolsnes_screen)

//======================================================================
//
//	lolsnes_screen, 256x192@8, 
//	+ palette 256 entries, not compressed
//	+ 342 tiles (t|f reduced) not compressed
//	+ regular map (flat), not compressed, 32x24 
//	Total size: 512 + 21888 + 1536 = 23936
//
//	Time-stamp: 2013-12-17, 00:59:22
//	Exported by Cearn's GBA Image Transmogrifier, v0.8.10
//	( http://www.coranac.com/projects/#grit )
//
//======================================================================

#ifndef GRIT_LOLSNES_SCREEN_H
#define GRIT_LOLSNES_SCREEN_H

#define lolsnes_screenTilesLen 21888
extern const unsigned int lolsnes_screenTiles[5472];

#define lolsnes_screenMapLen 1536
extern const unsigned short lolsnes_screenMap[768];

#define lolsnes_screenPalLen 512
extern const unsigned short lolsnes_screenPal[256];

#endif // GRIT_LOLSNES_SCREEN_H

//}}BLOCK(lolsnes_screen)
