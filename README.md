# lolSnes

A SNES emulator for the DS. My goal is to make rendering the most accurate possible, we'll see how far I can get.

**EmuCR please stop copypasting this every time you post a lolSnes build. It is almost always outdated. Either 
spend some time determining a more accurate feature list, or don't release Git builds.**

Considering the DS isn't powerful enough to handle a line-accurate software renderer AND emulate the CPU and other
funny things at the same time, I have to emulate graphics using the DS hardware. Reproducing the functions it
doesn't support (like per-tile priority or funky color effects) will be the challenge. How they will be emulated
will also depend on how games use them.

## Features

### What is currently supported

 * CPU -- 99% (all opcodes emulated; may miss a few unimportant bits about timing)
 * PPU -- ~50% (2bpp, 4bpp and 8bpp graphics, modes 0-4 and 7, mosaic, master brightness)
 * SPC700 -- 90% (most important stuff emulated)
 * DSP -- ~80% (code taken from SNemulDS. Emulates BRR sound with envelopes. Seems to lack noise and echo.)
 * DMA -- 50% (HDMA not supported yet, and DMA method is inefficient)
 * Support for large ROMs via intelligent ROM cache system
 * Regular joypad
 * Linear audio interpolation (the SNES does Gaussian interpolation, but the DS isn't powerful enough)

### What is NOT supported

 * Expansion chips
 * Multiplayer

100% accurate graphics emulation is impossible, but we'll do our best.

## How to use

Place `lolsnes.nds` in your flashcart's root folder (or wherever DS ROMs are). In the same folder, create a folder 
named `snes`, and place your ROMs in there.

lolSnes is able to properly detect the ROM type in most cases. Headered and headerless ROMs are supported, both
LoROM and HiROM.

Once it starts, you should see a (not so) fancy ROM selection menu. Select the ROM you want to play, and press
A or B. Then uh... see what happens. Some games run, others explode spectacularly or just do nothing.
