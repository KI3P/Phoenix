/*
 * TestATUBoard: exercise the relays of the K9HZ 100W antenna tuner one at a time.
 *
 * Use Teensy 4.1 board.
 *
 * Connect the ATU control board's BANDS connector (J10) to the radio's BANDS
 * connector (I2C bus Wire2) and supply 12 V to J11. Set the address jumpers:
 *   U5 MCP23017 at 0x20: close J4, J5 and J6
 *   U6 MCP23008 at 0x21: close J8 and J9, leave J7 open
 * (With every jumper open both chips would be at 0x27 and clash.)
 *
 * Open the serial monitor at 115200 baud, type a command and hit enter.
 * The WALK command energises each relay in turn so you can listen for the clicks.
 *
 * ATUBoard.cpp/.h is the library that will later be moved into PhoenixSketch/.
 * SDT.h in this folder is a minimal stand-in for PhoenixSketch/SDT.h.
 */

#include "SDT.h"
#include "ATUBoard.h"

// In Phoenix these are provided by Globals.cpp
uint64_t hardwareRegister = 0;
struct BIT bit_results;
void buffer_add(void){}

#define NUM_LINES 19                // control lines: K1-K18 plus K19/K20 together
#define DEFAULT_WALK_MS 1000

///////////////////////////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////////////////////////

void scanner(void){
    int nDevices = 0;
    Serial.println("Scanning Wire2 (BANDS connector)...");
    for (uint8_t address = 1; address < 127; address++){
        Wire2.beginTransmission(address);
        uint8_t error = Wire2.endTransmission();
        if (error == 0){
            Serial.print("I2C device found at address 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            switch (address){
                case ATU_MCP23017_ADDR: Serial.print("  (ATU MCP23017, L and C relays)"); break;
                case ATU_MCP23008_ADDR: Serial.print("  (ATU MCP23008, mode relays)"); break;
                case 0x24: Serial.print("  (BPF board MCP23017)"); break;
                case 0x25: Serial.print("  (LPF board MCP23017)"); break;
                case 0x27: Serial.print("  (an ATU expander with all address jumpers open?)"); break;
                case 0x28:
                case 0x29: Serial.print("  (LPF board AD7991)"); break;
                default: break;
            }
            Serial.println();
            nDevices++;
        } else if (error == 4){
            Serial.print("Unknown error at address 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);
        }
    }
    if (nDevices == 0) Serial.println("No I2C devices found");
    Serial.println("done\n");
}

// Relay number used to switch control line `line` (0-18)
uint8_t LineToRelay(uint8_t line){
    return line + 1;   // line 18 is K19 (and K20)
}

String RelayName(uint8_t line){
    if (line < 9) return "K" + String(line + 1) + " ";
    if (line < 18) return "K" + String(line + 1);
    return "K19+K20";
}

String LineDescription(uint8_t line){
    if (line < 8){
        return "L" + String(line + 1) + " " + String(ATUInductorValue_uH(line + 1), 2) + " uH series";
    }
    if (line < 16){
        return "C" + String(line - 7) + " " + String(ATUCapacitorValue_pF(line - 7), 0) + " pF shunt";
    }
    if (line == 16) return "LC-CL: caps at input (C-L)";
    if (line == 17) return "BAL OUT: balun output";
    return "BYPASS line: network in line";
}

String ModeSummary(void){
    String s = ATUNetworkEngaged() ? "Network IN line, " : "Network BYPASSED, ";
    s += ATUCapsAtInput() ? "caps at input (C-L), " : "caps at output (L-C), ";
    s += ATUBalancedOutput() ? "balanced (balun) output" : "coax output";
    return s;
}

void PrintState(void){
    bool present = bit_results.ATU_MCP23017_present && bit_results.ATU_MCP23008_present;
    uint32_t reg = GetATURegisterState();
    uint32_t hw = present ? GetATUMCPRegisters() : 0;

    Serial.println("Relay    Function                       Register  Readback");
    for (uint8_t line = 0; line < NUM_LINES; line++){
        String row = RelayName(line) + "  " + LineDescription(line);
        while (row.length() < 40) row += " ";
        bool r = (reg >> line) & 1;
        row += r ? "ON        " : "off       ";
        if (present){
            bool h = (hw >> line) & 1;
            row += h ? "ON" : "off";
            if (h != r) row += "   <-- MISMATCH";
        } else {
            row += "n/a";
        }
        Serial.println(row);
    }
    if (present && ((hw >> 19) & 0x1F)){
        Serial.println("Warning: MCP23008 GP3-GP7 (unused) read back non-zero: 0x"
                       + String((hw >> 16) & 0xFF, HEX));
    }
    Serial.println();
    Serial.println("Total L = " + String(GetATUInductance_uH(), 2) + " uH  (mask 0b"
                   + String(GetATUInductors(), BIN) + ")");
    Serial.println("Total C = " + String(GetATUCapacitance_pF(), 0) + " pF  (mask 0b"
                   + String(GetATUCapacitors(), BIN) + ")");
    Serial.println(ModeSummary());
    Serial.println("ATU register = 0x" + String(reg, HEX)
                   + (present ? ", readback = 0x" + String(hw & 0x7FFFF, HEX)
                              : ", expanders not initialized"));
}

void PrintHelp(void){
    Serial.println("Commands (type and hit enter):");
    Serial.println("K<n>       - Toggle relay Kn (1-20; K19 and K20 switch together)");
    Serial.println("L<n>       - Toggle inductor Ln (1-8)");
    Serial.println("C<n>       - Toggle capacitor Cn (1-8)");
    Serial.println("LM <0-255> - Set the inductor mask");
    Serial.println("CM <0-255> - Set the capacitor mask");
    Serial.println("LC / CL    - Caps at output (L-C) / caps at input (C-L)   [K17]");
    Serial.println("UNB / BAL  - Coax output / balun output                  [K18]");
    Serial.println("BYP / IN   - Bypass network / network in line         [K19+K20]");
    Serial.println("WALK [ms]  - Energise each relay in turn (default 1000 ms); any key aborts");
    Serial.println("ALL0       - Safe state: every relay off");
    Serial.println("ALL1       - Every relay on (check your 12 V supply can take it)");
    Serial.println("SCAN       - Scan the Wire2 I2C bus");
    Serial.println("INIT       - Retry initializing the ATU board");
    Serial.println("?          - This help");
}

// Delay that returns true early if a key is pressed
bool DelayOrKey(uint32_t ms){
    uint32_t start = millis();
    while (millis() - start < ms){
        if (Serial.available()) return true;
    }
    return false;
}

void DrainSerial(void){
    delay(50);
    while (Serial.available()) Serial.read();
}

void Walk(uint32_t ms){
    ResetATUBoard();
    Serial.println("Walking all relays, " + String(ms) + " ms on / " + String(ms)
                   + " ms off. Press enter to abort.");
    bool aborted = false;
    for (uint8_t line = 0; (line < NUM_LINES) && !aborted; line++){
        uint8_t k = LineToRelay(line);
        Serial.print(RelayName(line) + "  " + LineDescription(line) + "  -> ON");
        SetATURelay(k, true);
        aborted = DelayOrKey(ms);
        Serial.println(", OFF");
        SetATURelay(k, false);
        if (!aborted) aborted = DelayOrKey(ms);
    }
    ResetATUBoard();
    if (aborted){
        DrainSerial();
        Serial.println("Walk aborted; all relays off.");
    } else {
        Serial.println("Walk complete; all relays off.");
    }
}

// Parse the number after a prefix, e.g. "K12" -> 12. Returns -1 if not a number.
int32_t ParseNumber(const String &s, uint8_t prefixLength){
    String num = s.substring(prefixLength);
    num.trim();
    if (num.length() == 0) return -1;
    for (unsigned int i = 0; i < num.length(); i++){
        if (!isDigit(num.charAt(i))) return -1;
    }
    return num.toInt();
}

///////////////////////////////////////////////////////////////////////////////
// Arduino entry points
///////////////////////////////////////////////////////////////////////////////

void setup(){
    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && (millis() - start < 5000)) {}

    Serial.println("\nK9HZ ATU board test");
    Wire2.begin();

    while (InitializeATUBoard() != ESUCCESS){
        Serial.println("ATU board not found:");
        if (!bit_results.ATU_MCP23017_present)
            Serial.println("  MCP23017 (U5) missing at 0x" + String(ATU_MCP23017_ADDR, HEX)
                           + ": close J4, J5 and J6");
        if (!bit_results.ATU_MCP23008_present)
            Serial.println("  MCP23008 (U6) missing at 0x" + String(ATU_MCP23008_ADDR, HEX)
                           + ": close J8 and J9, leave J7 open");
        Serial.println("  Also check 12 V on J11 and the BANDS cable.");
        scanner();
        Serial.println("Retrying in 5 s (or press enter to continue without the board)...");
        if (DelayOrKey(5000)){
            DrainSerial();
            Serial.println("Continuing without the ATU board; use INIT to retry.");
            break;
        }
    }
    Serial.println();
}

void loop(){
    Serial.println("Current state:");
    PrintState();
    Serial.println();
    PrintHelp();

    while (Serial.available() == 0) {}       // wait for data available
    String selection = Serial.readString();  // read until timeout
    selection.trim();                        // remove any \r \n whitespace at the end
    selection.toUpperCase();
    Serial.println("> " + selection);

    int32_t n;
    if (selection == "?" || selection.length() == 0){
        // Just reprint the state and help
    } else if (selection == "LC"){
        Serial.println("Caps at output (L-C)");
        ATUSelectCapsAtOutput();
    } else if (selection == "CL"){
        Serial.println("Caps at input (C-L)");
        ATUSelectCapsAtInput();
    } else if (selection == "UNB"){
        Serial.println("Coax output");
        ATUSelectUnbalancedOutput();
    } else if (selection == "BAL"){
        Serial.println("Balun output");
        ATUSelectBalancedOutput();
    } else if (selection == "BYP"){
        Serial.println("Network bypassed");
        ATUBypassNetwork();
    } else if (selection == "IN"){
        Serial.println("Network in line");
        ATUEngageNetwork();
    } else if (selection == "ALL0"){
        Serial.println("All relays off");
        ResetATUBoard();
    } else if (selection == "ALL1"){
        Serial.println("All relays on. Each 12 V coil draws current: watch the supply.");
        SetATULC(0xFF, 0xFF);
        ATUSelectCapsAtInput();
        ATUSelectBalancedOutput();
        ATUEngageNetwork();
    } else if (selection == "SCAN"){
        scanner();
    } else if (selection == "INIT"){
        if (InitializeATUBoard() == ESUCCESS){
            Serial.println("ATU board initialized");
        } else {
            Serial.println("ATU board initialization failed");
            scanner();
        }
    } else if (selection.startsWith("WALK")){
        n = ParseNumber(selection, 4);
        Walk((n > 0) ? (uint32_t)n : DEFAULT_WALK_MS);
    } else if (selection.startsWith("LM")){
        n = ParseNumber(selection, 2);
        if ((n >= 0) && (n <= 255)){
            Serial.println("Inductor mask = " + String(n));
            SetATUInductors((uint8_t)n);
        } else {
            Serial.println("LM needs a value from 0 to 255");
        }
    } else if (selection.startsWith("CM")){
        n = ParseNumber(selection, 2);
        if ((n >= 0) && (n <= 255)){
            Serial.println("Capacitor mask = " + String(n));
            SetATUCapacitors((uint8_t)n);
        } else {
            Serial.println("CM needs a value from 0 to 255");
        }
    } else if (selection.startsWith("K")){
        n = ParseNumber(selection, 1);
        if ((n >= 1) && (n <= ATU_NUM_RELAYS)){
            bool on = !GetATURelay((uint8_t)n);
            if (n >= 19) Serial.println("K19 and K20 share a control line and switch together");
            Serial.println("K" + String(n) + (on ? " -> ON" : " -> off"));
            SetATURelay((uint8_t)n, on);
        } else {
            Serial.println("Relay number must be 1 to 20");
        }
    } else if (selection.startsWith("L")){
        n = ParseNumber(selection, 1);
        if ((n >= 1) && (n <= ATU_NUM_INDUCTORS)){
            uint8_t mask = GetATUInductors() ^ (1 << (n - 1));
            Serial.println("Toggling L" + String(n));
            SetATUInductors(mask);
        } else {
            Serial.println("Inductor number must be 1 to 8");
        }
    } else if (selection.startsWith("C")){
        n = ParseNumber(selection, 1);
        if ((n >= 1) && (n <= ATU_NUM_CAPACITORS)){
            uint8_t mask = GetATUCapacitors() ^ (1 << (n - 1));
            Serial.println("Toggling C" + String(n));
            SetATUCapacitors(mask);
        } else {
            Serial.println("Capacitor number must be 1 to 8");
        }
    } else {
        Serial.println("Unknown command: " + selection);
    }

    Serial.println("-------------------------\n");
}
