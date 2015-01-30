LiFeLight software
==================

[LiFePO4wered/USB]: http://lifepo4wered.com
[MSP430G2210]: http://www.ti.com/product/msp430g2210
[CCS]: http://www.ti.com/tool/ccstudio
[LiFeLight]: https://github.com/xorbit/LiFeLight-HW

This is the C source code implemented using [CCS][] for the [LiFeLight][] demo board.  LiFeLight is a board developed as a soldering kit for the Maker Faire, that can be added to a [LiFePO4wered/USB][] module to create a programmable touch flashlight.  It's a good example of getting decent functionality with minimal, low cost hardware.  It has the following subsystems:

Touch sensor
------------

The touch sensor is a simple PCB pad.  All touch detection functionality is implemented in software, using a simple interrupt driven charge/discharge scheme.

LED booster
-----------

To have high light output using various colors of LED (including white), using a supply voltage that can be as low as 2V, a simple booster circuit was implemented.  The software drives the LED output at ~100kHz.  Yes, the LED has to be mounted "backwards", because it is powered from the inductor's flyback voltage.

Microcontroller
---------------

All LiFeLight functionality is implemented in software that runs on a low cost [MSP430G2210][] microcontroller.
