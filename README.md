FlipBits is a small hardware and software project for programming legacy EPROMs used in pinball machines.

Currently only following Winbond EEPROMs are supported, although it is very simple to add support for other standard UV 27xxx EPROMs:
* W27C257 - 32kb Electrically erasable EPROM
* W27C512 - 64kb Electrically erasable EPROM
* W27E040 - 512kb Electrically erasable EPROM

These EPROMs can be erased electrically by appying 14V to some pins.

An [Arduino Nano](https://store.arduino.cc/arduino-nano) is used to control the hardware.

Schematics for [kicad](http://kicad-pcb.org/) are provided.
