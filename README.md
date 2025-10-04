# PPM to USB adapter using Raspberry Pi Pico

A firmware for Pi Pico that converts PPM signal from an RC remote transmitter to USB HID joystick. It supports 8 channels (HID driver limit).
I tested this with my old Futaba F14.

**Note:** The rp2040 is a 3,3V device, so if your PPM output is 5V, you MUST insert a voltage divider or a 2,7V Zener diode (or better both..) to prevent damage to your Pico.

In case of the Futaba, the PPM signal is only 2V, so I also needed a simple op-amp comparator to raise it.

Use at your own risk!

**Credits:**  
Based on the USB HID examples from the SDK.
