This folder contains Arduino sketches to test your T41 radio as you build it.

# Board tests

## TestMainAndFrontPanel

This tests the main board functions and the front panel (including the display). Load this sketch once you've built the main board and front panel to ensure that your input/output is working correctly.

## TestRFBoard

This sketch lets you control each of the functions of the RF board for debugging purposes. It lets you set the frequency, control the cal switch, the attenuators, etc. individually. Use this sketch if you need to debug your newly-built RF board.

## TestLPFBoard

This sketch lets you control each of the functions of the K9HZ LPF board for debugging purposes. Use this sketch if you need to debug your newly-built LPF board.


## TestBPFBoard

This sketch lets you select the filter channels on the BPF board. Useful for measuring filter responses.

# Helper functions

## i2cscan

This sketch searches for all I2C devices connected to the Teensy and prints a list, helpfully matching them up to the expected addresses of the T41 hardware. Use this sketch if you're having trouble communicating with the boards.
