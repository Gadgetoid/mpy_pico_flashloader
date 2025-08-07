# .mpy FlashLoader for MicroPython on the Pico

This project does one thing- flash a payload to a Raspberry Pi Pico (RP2040) from
MicroPython.

It uses a C-based .mpy module which runs from RAM (so be careful that your payload
fits into RAM), erases, flashes and resets the Pico.

`import bootstrap` will more or less instantly flash blink and reset your Pico,
destroying MicroPython in the process (albeit your files will remain, usually.)

It's intended to act as a first stage to load a more complex bootloader that
takes over from MicroPython and could -

* Upgrade an in-field MicroPython firmware to a C++ one
* Provide a low-level bootloader for OTA MicroPython updates
* Simply erase the whole chip, buwahahahahaha!
* Flash a firmware file from LittleFS or FAT on a micro SD card