
## [2026-06-14] decision | Mapped PE4312 attenuator control bits (MCP23017 U21 → PE4312)
Owner-confirmed + firmware-cross-checked. MCP23017 U21 (0x27): GPIOA → RX attenuator (RF-board
slot U3), GPIOB → TX attenuator (slot U11; U12 is the dual-clock daughter). PE4312 chip is U1
within the daughterboard sheet. Low 6 GPIO bits = attenuation word: bit0→pin20 C0.5(0.5dB),
bit1→19 C1(1dB), bit2→17 C2(2dB), bit3→16 C4(4dB), bit4→15 C8(8dB), bit5→1 C16(16dB); PE4312
pin18=GND. Matches firmware SetAttenuator round(2·atten_dB)&0x3F → writeGPIOA(RX)/writeGPIOB(TX)
(RFBoard.cpp:276,67-68,102-103). Note: owner said TX="U12" but schematic shows U11=attenuator
daughter / U12=dualclock daughter — wiki uses U11 (flagged for confirmation). Updated [[rf-board]]
(mapping table + resolved open Q), [[t41-ep-schematics]]; struck item in [[documentation-todos]].
Remaining: upper MCP bits (GPA6/7,GPB6/7) function still untraced.
