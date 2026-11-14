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

## TestATUBoard

This sketch lets you switch each relay of the K9HZ 100W antenna tuner individually: the eight series inductors (K1-K8), the eight shunt capacitors (K9-K16), the LC/CL capacitor position (K17), the balanced output (K18), and the network bypass (K19+K20). The `WALK` command energises each relay in turn so you can listen for the clicks. After every command it prints the relay states and reads the I/O expanders back over I2C to confirm the write.

Connect the control board's BANDS connector (J10) to the radio's BANDS connector (I2C bus Wire2) and supply 12 V to J11. Before you start, set the address jumpers so the two expanders don't clash with each other or with the LPF/BPF boards:

* U5 MCP23017 (L and C relays) at 0x20: close J4, J5 and J6
* U6 MCP23008 (mode relays) at 0x21: close J8 and J9, and leave J7 open

`ATUBoard.cpp/.h` in this folder is the ATU control library. It will later be moved into PhoenixSketch.

# Helper functions

## i2cscan

This sketch searches for all I2C devices connected to the Teensy and prints a list, helpfully matching them up to the expected addresses of the T41 hardware. Use this sketch if you're having trouble communicating with the boards.
