lolSnes

A SNES emulator for the DS. My goal is to make rendering the most accurate possible, we'll see how far I can get.

Considering the DS isn't powerful enough to handle a line-accurate software renderer AND emulate the CPU and other
funny things at the same time, I have to emulate graphics using the DS hardware. Reproducing the functions it
doesn't support (like per-tile priority or funky color effects) will be the challenge. How they will be emulated
will also depend on how games use them.

What is currently supported

* CPU -- 99% (all opcodes emulated; may miss a few unimportant bits about timing)
* PPU -- ~20% (supports 2bpp and 4bpp BGs with scrolling)

Next priority

* SPC700 support -- any ROM that isn't a basic test, requires basic SPC700 functionality
* More PPU stuff -- OBJ support, and perhaps 8bpp BGs
* Proper interface (or atleast letting users select a ROM)


100% accurate graphics emulation is impossible, but we'll do our best.


How to use

Place lolsnes.nds in your flashcart's root folder (or wherever DS ROMs are). In the same folder, create a folder 
named 'snes', and place your ROM in there, under the name 'rom.smc'.

lolSnes is able to properly detect the ROM type in most cases. Headered and headerless ROMs are supported, both
LoROM and HiROM.

lolSnes isn't very exciting, though. Few things will run without entering an endless loop due to the lack of
SPC700 support.