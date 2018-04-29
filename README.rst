Nofrendo ported to the PocketSprite
-----------------------------------

This allows you to run NES/Famicom ROMs on the PocketSprite, in glorious squint-o-vision.

*NOTE*: THIS IS NOT PRODUCTION-QUALITY CODE AND NOT AN OFFICIALLY SUPPORTED EMULATOR! It misses the features that
the official PocketSprite ports of Gnuboy and SMSPlus emulators have, and pushes the scaling algorithms
(in our opinion) past their breaking point. This emulator is released because of public interest in it. While
the PocketSprite team probably won't develop this code further, we're always open to merge requests adding
features. Just remember: if this breaks, you get to keep both pieces.

At the moment, this emulator misses any and all savegame and snapshot support, meaning that every time
you power down the emulator, you loose all game state. Both snapshots and cartridge SRAM are not supported.

Original code in this repository is Copyright (C) 2016 Espressif Systems, licensed under the Apache License 
2.0 as described in the file LICENSE. PocketSprite modifications to this code fall under the same license. 
Code in the components/nofrendo directory is Copyright (c) 1998-2000 Matthew Conte (matt@conte.com) and 
licensed under the GPLv2.

