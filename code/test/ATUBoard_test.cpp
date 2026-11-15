// Tests for the ATU (antenna tuner) driver, its menu and its AT CAT command.
// This executable is built with ATU_PRESENT defined (see CMakeLists.txt).

#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/ATUBoard.h"

#ifndef ATU_PRESENT
#error "ATUBoard_test.cpp must be compiled with ATU_PRESENT defined"
#endif

extern char *command_parser(char *command);
extern size_t primaryMenuIndex;
extern size_t secondaryMenuIndex;

// The whole ATU field: bits 33-51 of hardwareRegister
static const uint64_t ATU_FIELD = 0x7FFFFULL << ATULLSB;

class ATUBoardTest : public ::testing::Test {
protected:
    void SetUp() override {
        InitializeRFHardware();   // includes InitializeATUBoard()
        PutInReceive();
        ResetATUBoard();
    }

    void TearDown() override {
        PutInReceive();
        ResetATUBoard();
    }

    static void PutInReceive(void) {
        modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
        UpdateRFHardwareState();
    }

    static void PutInTransmit(void) {
        modeSM.state_id = ModeSm_StateId_SSB_TRANSMIT;
        UpdateRFHardwareState();
    }

    // CAT commands are parsed in place, so pass a writable copy
    static std::string Cat(const char *cmd) {
        char buf[64];
        strncpy(buf, cmd, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return std::string(command_parser(buf));
    }
};

// ================== DRIVER ==================

TEST_F(ATUBoardTest, InitializeFindsBothExpandersAndBootsBypassed) {
    EXPECT_EQ(InitializeATUBoard(), ESUCCESS);
    EXPECT_TRUE(bit_results.ATU_MCP23017_present);
    EXPECT_TRUE(bit_results.ATU_MCP23008_present);
    EXPECT_EQ(GetATURegisterState(), 0u);
    EXPECT_FALSE(ATUNetworkEngaged());
}

TEST_F(ATUBoardTest, InductorAndCapacitorMasksLandInTheirFields) {
    SetATUInductors(0xA5);
    EXPECT_EQ(GetATUInductors(), 0xA5);
    EXPECT_EQ((hardwareRegister >> ATULLSB) & 0xFF, 0xA5u);
    SetATUCapacitors(0x3C);
    EXPECT_EQ(GetATUCapacitors(), 0x3C);
    EXPECT_EQ((hardwareRegister >> ATUCLSB) & 0xFF, 0x3Cu);
    // Setting one does not disturb the other
    EXPECT_EQ(GetATUInductors(), 0xA5);
    SetATULC(0x01, 0x80);
    EXPECT_EQ(GetATUInductors(), 0x01);
    EXPECT_EQ(GetATUCapacitors(), 0x80);
}

TEST_F(ATUBoardTest, ModeRelaysSetAndClearTheirBits) {
    ATUEngageNetwork();
    EXPECT_EQ(GET_BIT(hardwareRegister, ATUNETWORKBIT), 1u);
    ATUSelectBalancedOutput();
    EXPECT_EQ(GET_BIT(hardwareRegister, ATUBALANCEDBIT), 1u);
    ATUSelectCapsAtInput();
    EXPECT_EQ(GET_BIT(hardwareRegister, ATUCAPINPUTBIT), 1u);
    EXPECT_TRUE(ATUNetworkEngaged() && ATUBalancedOutput() && ATUCapsAtInput());

    ATUBypassNetwork();
    ATUSelectUnbalancedOutput();
    ATUSelectCapsAtOutput();
    EXPECT_EQ(GetATURegisterState(), 0u);
}

TEST_F(ATUBoardTest, RelayNumbersMapToTheSchematic) {
    for (uint8_t k = 1; k <= 8; k++) {
        ResetATUBoard();
        EXPECT_EQ(SetATURelay(k, true), ESUCCESS);
        EXPECT_EQ(GetATUInductors(), 1 << (k - 1)) << "K" << (int)k;
    }
    for (uint8_t k = 9; k <= 16; k++) {
        ResetATUBoard();
        SetATURelay(k, true);
        EXPECT_EQ(GetATUCapacitors(), 1 << (k - 9)) << "K" << (int)k;
    }
    ResetATUBoard();
    SetATURelay(17, true);
    EXPECT_TRUE(ATUCapsAtInput());
    SetATURelay(18, true);
    EXPECT_TRUE(ATUBalancedOutput());
    // K19 and K20 share one control line
    SetATURelay(20, true);
    EXPECT_TRUE(ATUNetworkEngaged());
    EXPECT_TRUE(GetATURelay(19));
    SetATURelay(19, false);
    EXPECT_FALSE(GetATURelay(20));

    EXPECT_EQ(SetATURelay(0, true), EFAIL);
    EXPECT_EQ(SetATURelay(21, true), EFAIL);
    EXPECT_FALSE(GetATURelay(21));
}

TEST_F(ATUBoardTest, SetATUStateSetsEveryFieldAtOnce) {
    SetATUState(true, false, true, 32, 16);
    EXPECT_TRUE(ATUNetworkEngaged());
    EXPECT_FALSE(ATUBalancedOutput());
    EXPECT_TRUE(ATUCapsAtInput());
    EXPECT_EQ(GetATUInductors(), 32);
    EXPECT_EQ(GetATUCapacitors(), 16);
    // Bit 0 = L1 ... bit 18 = network
    EXPECT_EQ(GetATURegisterState(), (1u << 18) | (1u << 16) | (16u << 8) | 32u);
}

TEST_F(ATUBoardTest, UpdateWritesOnlyChangedPorts) {
    SetATULC(0x12, 0x34);
    ATUEngageNetwork();
    EXPECT_EQ(GetATUMCPAOld(), 0x12);
    EXPECT_EQ(GetATUMCPBOld(), 0x34);
    EXPECT_EQ(GetATUMCPGPOld(), 0x04);

    // Change the register without touching hardware, then update
    SetATURegisterState(0x00056);   // L = 0x56, C = 0, mode = 0
    EXPECT_EQ(GetATUMCPAOld(), 0x12);   // not written yet
    UpdateATURegisters();
    EXPECT_EQ(GetATUMCPAOld(), 0x56);
    EXPECT_EQ(GetATUMCPBOld(), 0x00);
    EXPECT_EQ(GetATUMCPGPOld(), 0x00);
}

TEST_F(ATUBoardTest, ModeRelayReadbackMatchesRegister) {
    // The MCP23X17 mock reverses the GPIOA/GPIOB byte order relative to the
    // Adafruit library, so only the MCP23008 byte is compared here.
    SetATUState(true, true, false, 0, 0);
    EXPECT_EQ((GetATUMCPRegisters() >> 16) & 0xFF, 0x06u);
}

TEST_F(ATUBoardTest, NominalTotals) {
    SetATULC(0xFF, 0xFF);
    EXPECT_NEAR(GetATUInductance_uH(), 17.22f, 0.001f);
    EXPECT_NEAR(GetATUCapacitance_pF(), 3869.0f, 0.01f);
    SetATULC(0x00, 0x00);
    EXPECT_EQ(GetATUInductance_uH(), 0.0f);
    EXPECT_EQ(ATUInductorValue_uH(9), 0.0f);
    EXPECT_EQ(ATUCapacitorValue_pF(0), 0.0f);
}

TEST_F(ATUBoardTest, StateSurvivesOtherBoardsAndTransmit) {
    SetATUState(true, false, true, 0x5A, 0xC3);
    const uint64_t atu = hardwareRegister & ATU_FIELD;

    SelectLPFBand(BAND_20M);
    SelectBPFBand(BAND_20M);
    SelectAntenna(1);
    SetRXAttenuation(10.0);
    SetTXAttenuation(20.0);
    EXPECT_EQ(hardwareRegister & ATU_FIELD, atu);

    PutInTransmit();
    EXPECT_EQ(hardwareRegister & ATU_FIELD, atu);
    PutInReceive();
    EXPECT_EQ(hardwareRegister & ATU_FIELD, atu);
}

TEST_F(ATUBoardTest, ChangesAllowedOnlyInReceive) {
    EXPECT_TRUE(ATUChangeAllowed());
    PutInTransmit();
    EXPECT_FALSE(ATUChangeAllowed());
    PutInReceive();
    EXPECT_TRUE(ATUChangeAllowed());
}

// ================== CAT: AT ==================

TEST_F(ATUBoardTest, CatWriteThenRead) {
    EXPECT_EQ(Cat("AT101032016;"), "");
    EXPECT_TRUE(ATUNetworkEngaged());
    EXPECT_FALSE(ATUBalancedOutput());
    EXPECT_TRUE(ATUCapsAtInput());
    EXPECT_EQ(GetATUInductors(), 32);
    EXPECT_EQ(GetATUCapacitors(), 16);

    std::string reply = Cat("AT;");
    // Register 0x51020 = network (bit 18) + caps at input (bit 16) + C=16 + L=32
    EXPECT_EQ(reply.substr(0, 18), "AT101032016,51020,");
    EXPECT_EQ(reply.substr(reply.size() - 3), ",1;");

    EXPECT_EQ(Cat("AT000000000;"), "");
    EXPECT_EQ(GetATURegisterState(), 0u);
    EXPECT_EQ(Cat("AT;").substr(0, 18), "AT000000000,00000,");
}

TEST_F(ATUBoardTest, CatMaxMasks) {
    EXPECT_EQ(Cat("AT111255255;"), "");
    EXPECT_EQ(GetATURegisterState(), 0x7FFFFu);
}

TEST_F(ATUBoardTest, CatRejectsBadInput) {
    SetATUState(true, false, false, 7, 9);
    const uint32_t before = GetATURegisterState();
    EXPECT_EQ(Cat("AT100256000;"), "?;");   // L mask out of range
    EXPECT_EQ(Cat("AT100000300;"), "?;");   // C mask out of range
    EXPECT_EQ(Cat("AT200000000;"), "?;");   // flag not 0/1
    EXPECT_EQ(Cat("AT10000A000;"), "?;");   // not a digit
    EXPECT_EQ(Cat("AT1000000;"), "?;");     // too short
    EXPECT_EQ(GetATURegisterState(), before);
}

TEST_F(ATUBoardTest, CatRefusesWritesWhileTransmitting) {
    SetATUState(false, false, false, 1, 1);
    PutInTransmit();
    EXPECT_EQ(Cat("AT100128128;"), "?;");
    EXPECT_EQ(GetATUInductors(), 1);
    EXPECT_FALSE(ATUNetworkEngaged());
    // Reading is fine while transmitting
    EXPECT_EQ(Cat("AT;").substr(0, 11), "AT000001001");
}

// ================== MENU ==================

namespace {
enum { ITEM_ENABLE = 0, ITEM_TABLE = 1, ITEM_CLEAR = 2, ITEM_TUNE = 3, ITEM_POWER = 4, ITEM_OUTPUT = 5 };

/// Select a function item of the ATU menu, as the front panel does: the menu goes
/// home and then runs the item
void Select(int item) {
    primaryMenuIndex = NUM_PRIMARY_MENUS - 1;
    secondaryMenuIndex = item;
    UISm_start(&uiSM);
    uiSM.state_id = UISm_StateId_SECONDARY_MENU;
    UISm_dispatch_event(&uiSM, UISm_EventId_SELECT);   // -> UPDATE
    UISm_dispatch_event(&uiSM, UISm_EventId_HOME);
    ((void (*)(void))ATUOptions[item].func)();
    ATUMenuRefreshLabels();
}

int32_t Ant(void) { return ED.antennaSelection[ED.currentBand[ED.activeVFO]]; }
}  // namespace

class ATUMenuTest : public ATUBoardTest {
protected:
    void SetUp() override {
        ATUBoardTest::SetUp();
        for (int j = 0; j < ATU_NUM_ANTENNAS; j++){
            ED.ATU_Enabled[j] = true;
            ED.ATU_BalunOut[j] = false;
            for (int b = 0; b < NUMBER_OF_BANDS; b++) ATUMemoryForget(b, j);
        }
        ED.antennaSelection[ED.currentBand[ED.activeVFO]] = 0;
        ATUMenuDisarm();
        ATUMenuRefreshLabels();
    }
    void TearDown() override {
        for (int j = 0; j < ATU_NUM_ANTENNAS; j++){ ED.ATU_Enabled[j] = true; ED.ATU_BalunOut[j] = false; }
        ED.antennaSelection[ED.currentBand[ED.activeVFO]] = 0;
        ATUBoardTest::TearDown();
    }
};

TEST_F(ATUMenuTest, MenuIsLastPrimaryEntryWithTheSimpleItems) {
    EXPECT_EQ(NUM_PRIMARY_MENUS, 10);
    EXPECT_STREQ(primaryMenu[NUM_PRIMARY_MENUS - 1].label, "ATU");
    EXPECT_EQ(primaryMenu[NUM_PRIMARY_MENUS - 1].secondary, ATUOptions);
    EXPECT_EQ(primaryMenu[NUM_PRIMARY_MENUS - 1].length, (size_t)ATU_MENU_ITEMS);
    EXPECT_STREQ(ATUOptions[ITEM_ENABLE].label, "Bypass ATU");
    EXPECT_STREQ(ATUOptions[ITEM_TABLE].label, "Tuning status table");
    EXPECT_STREQ(ATUOptions[ITEM_CLEAR].label, "Clear memory, antenna 0");
    EXPECT_STREQ(ATUOptions[ITEM_TUNE].label, "Tune");
    EXPECT_STREQ(ATUOptions[ITEM_POWER].label, "Tune power (W)");
    EXPECT_EQ(ATUOptions[ITEM_POWER].varPam->variable, &ED.ATU_TunePower_W);
    EXPECT_STREQ(ATUOptions[ITEM_OUTPUT].label, "Output: coax");
    // Existing positions that other tests rely on are unchanged
    EXPECT_STREQ(primaryMenu[0].label, "RF Options");
    EXPECT_STREQ(primaryMenu[6].label, "Calibration");
}

TEST_F(ATUMenuTest, BypassAndEnableArePerAntenna) {
    const uint32_t stored = 0x40000u | (5u << 8) | 9u;   // LC, C 5, L 9, network in
    ATUMemoryStore(ED.currentBand[ED.activeVFO], 0, stored, 14200, 1.2f);
    ATUApplyForAntenna(ED.currentBand[ED.activeVFO], 0);
    EXPECT_EQ(GetATURegisterState(), stored);
    Select(ITEM_ENABLE);                        // Bypass ATU
    EXPECT_FALSE(ED.ATU_Enabled[0]);
    EXPECT_TRUE(ED.ATU_Enabled[1]);
    EXPECT_FALSE(ATUNetworkEngaged());
    EXPECT_STREQ(ATUOptions[ITEM_ENABLE].label, "Enable ATU");
    Select(ITEM_ENABLE);                        // Enable ATU: the stored setting is back
    EXPECT_TRUE(ED.ATU_Enabled[0]);
    EXPECT_EQ(GetATURegisterState(), stored);
    EXPECT_STREQ(ATUOptions[ITEM_ENABLE].label, "Bypass ATU");
    // The label follows the antenna chosen in RF Options
    ED.ATU_Enabled[2] = false;
    ED.antennaSelection[ED.currentBand[ED.activeVFO]] = 2;
    ATUMenuRefreshLabels();
    EXPECT_STREQ(ATUOptions[ITEM_ENABLE].label, "Enable ATU");
    EXPECT_STREQ(ATUOptions[ITEM_CLEAR].label, "Clear memory, antenna 2");
}

TEST_F(ATUMenuTest, OutputSwitchesTheBalunRelayForThisAntenna) {
    Select(ITEM_OUTPUT);
    EXPECT_TRUE(ED.ATU_BalunOut[0]);
    EXPECT_TRUE(ATUBalancedOutput());
    EXPECT_FALSE(ATUNetworkEngaged());
    EXPECT_STREQ(ATUOptions[ITEM_OUTPUT].label, "Output: balun");
    Select(ITEM_OUTPUT);
    EXPECT_FALSE(ED.ATU_BalunOut[0]);
    EXPECT_FALSE(ATUBalancedOutput());
}

TEST_F(ATUMenuTest, TogglesAreRefusedWhileTransmitting) {
    PutInTransmit();
    Select(ITEM_ENABLE);
    Select(ITEM_OUTPUT);
    EXPECT_TRUE(ED.ATU_Enabled[0]);
    EXPECT_FALSE(ED.ATU_BalunOut[0]);
    EXPECT_FALSE(ATUBalancedOutput());
    PutInReceive();
}

TEST_F(ATUMenuTest, ClearNeedsASecondPressAndClearsOnlyThisAntenna) {
    ATUMemoryStore(BAND_20M, 0, 0x40101u, 14200, 1.2f);
    ATUMemoryStore(BAND_40M, 0, 0x40202u, 7100, 1.3f);
    ATUMemoryStore(BAND_20M, 1, 0x40303u, 14200, 1.1f);
    Select(ITEM_CLEAR);
    // Armed: back in the menu on the same item, nothing cleared yet
    EXPECT_EQ(uiSM.state_id, UISm_StateId_SECONDARY_MENU);
    EXPECT_EQ(secondaryMenuIndex, (size_t)ITEM_CLEAR);
    EXPECT_STREQ(ATUOptions[ITEM_CLEAR].label, "Press again to clear");
    EXPECT_TRUE(ATUMemoryGet(BAND_20M, 0, NULL, NULL, NULL));
    Select(ITEM_CLEAR);
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 0, NULL, NULL, NULL));
    EXPECT_FALSE(ATUMemoryGet(BAND_40M, 0, NULL, NULL, NULL));
    EXPECT_TRUE(ATUMemoryGet(BAND_20M, 1, NULL, NULL, NULL));
    EXPECT_STREQ(ATUOptions[ITEM_CLEAR].label, "Clear memory, antenna 0");
}

TEST_F(ATUMenuTest, MovingOffOrWaitingCancelsAClear) {
    ATUMemoryStore(BAND_20M, 0, 0x40101u, 14200, 1.2f);
    Select(ITEM_CLEAR);
    IncrementSecondaryMenu();                   // moved off the item
    Select(ITEM_CLEAR);                         // arms again rather than clearing
    EXPECT_TRUE(ATUMemoryGet(BAND_20M, 0, NULL, NULL, NULL));
    EXPECT_STREQ(ATUOptions[ITEM_CLEAR].label, "Press again to clear");
    AddMillisTime(10001);                       // too late
    Select(ITEM_CLEAR);
    EXPECT_TRUE(ATUMemoryGet(BAND_20M, 0, NULL, NULL, NULL));
    Select(ITEM_CLEAR);
    EXPECT_FALSE(ATUMemoryGet(BAND_20M, 0, NULL, NULL, NULL));
}

TEST_F(ATUMenuTest, StatusTableIsAPageThatHomeLeaves) {
    while (GetInterrupt() != iNONE) {}
    Select(ITEM_TABLE);
    ConsumeInterrupt();
    EXPECT_EQ(uiSM.state_id, UISm_StateId_ATU_STATUS);
    EXPECT_TRUE(uiSM.vars.clearScreen);
    DrawDisplay();                              // draws the table once
    EXPECT_FALSE(uiSM.vars.clearScreen);
    SetButton(HOME_SCREEN);
    SetInterrupt(iBUTTON_PRESSED);
    ConsumeInterrupt();
    EXPECT_EQ(uiSM.state_id, UISm_StateId_HOME);
}
