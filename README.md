# .mpy FlashLoader for MicroPython on the Pico

This project does one thing- flash a payload to a Raspberry Pi Pico (RP2040) from
MicroPython.

It uses a C-based .mpy module (or Dynamic Native MOdule) which runs from RAM
(so be careful that your payload fits into RAM), erases, flashes and resets the Pico.

`import bootstrap` will more or less instantly flash blink and reset your Pico,
destroying MicroPython in the process (albeit your files will remain, usually.)

It's intended to act as a first stage to load a more complex bootloader that
takes over from MicroPython and could -

* Upgrade an in-field MicroPython firmware to a C++ one
* Provide a low-level bootloader for OTA MicroPython updates
* Simply erase the whole chip, buwahahahahaha!
* Flash a firmware file from LittleFS or FAT on a micro SD card

It's up to you how you get the .mpy file onto your (possibly remote) Pico
MicroPython board, and what payload you run. Good luck!

This codebase assumes a working knowledge of how to compile Dynamic Native Modules,
see: https://github.com/micropython/micropython/tree/master/examples/natmod

If you want to flash your payload to somewhere less spicy then offset 0, you will
also need to know how to hack on the Pico SDK linker script to relocate your binary.