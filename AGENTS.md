This project randomizes an NTSC Top Gear 2 ROM. It's a LoROM game. There's a legally dumped Top Gear 2.smc file, including header, present on my local harddrive (not in git).
main.cpp checks for a header and if so, removes the first 0x200 bytes. Any addresses listed in main.cpp assume there is no header and this should not be changed. It also contains a lot of commented out code due to experimentation, keep that code there.

In the reverse-engineering-docs I've got various files with my findings thus far. The files there incredibly big and full of addresses for a specific purpose. To save on tokens, only read them if I explicitly ask you to.
