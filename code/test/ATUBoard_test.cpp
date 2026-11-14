// Tests for the ATU (antenna tuner) driver, its menu and its AT CAT command.
// This executable is built with ATU_PRESENT defined (see CMakeLists.txt).

#include "gtest/gtest.h"

#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/ATUBoard.h"

#ifndef ATU_PRESENT
#error "ATUBoard_test.cpp must be compiled with ATU_PRESENT defined"
#endif

extern char *command_parser(char *command);
extern struct SecondaryMenuOption ATUOptions[6];
extern void SyncATUMenuVariables(void);

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

TEST_F(ATUBoardTest, MenuIsLastPrimaryEntry) {
    EXPECT_EQ(NUM_PRIMARY_MENUS, 10);
    EXPECT_STREQ(primaryMenu[NUM_PRIMARY_MENUS - 1].label, "ATU");
    EXPECT_EQ(primaryMenu[NUM_PRIMARY_MENUS - 1].secondary, ATUOptions);
    EXPECT_EQ(primaryMenu[NUM_PRIMARY_MENUS - 1].length, 6u);
    // Existing positions that other tests rely on are unchanged
    EXPECT_STREQ(primaryMenu[0].label, "RF Options");
    EXPECT_STREQ(primaryMenu[6].label, "Calibration");
}

// Apply one encoder step to a menu item and run its post-update callback
static void Step(int item, bool up) {
    SecondaryMenuOption &opt = ATUOptions[item];
    if (up) IncrementVariable(opt.varPam); else DecrementVariable(opt.varPam);
    ((void (*)(void))opt.postUpdateFunc)();
}

TEST_F(ATUBoardTest, MenuItemsDriveTheRelays) {
    SyncATUMenuVariables();
    Step(0, true);                        // Network in line
    EXPECT_TRUE(ATUNetworkEngaged());
    Step(1, true);                        // Caps at input
    EXPECT_TRUE(ATUCapsAtInput());
    Step(2, true);                        // Balun output
    EXPECT_TRUE(ATUBalancedOutput());
    Step(3, true); Step(3, true);         // L mask 0 -> 2
    EXPECT_EQ(GetATUInductors(), 2);
    Step(4, true);                        // C mask 0 -> 1
    EXPECT_EQ(GetATUCapacitors(), 1);
    Step(4, false); Step(4, false);       // clamps at 0
    EXPECT_EQ(GetATUCapacitors(), 0);

    // Bypass all
    ((void (*)(void))ATUOptions[5].func)();
    EXPECT_EQ(GetATURegisterState(), 0u);
    EXPECT_EQ(*(int32_t *)ATUOptions[3].varPam->variable, 0);
}

TEST_F(ATUBoardTest, MenuPicksUpChangesMadeOverCat) {
    Cat("AT100077000;");
    UpdateArrayVariables();
    EXPECT_TRUE(*(bool *)ATUOptions[0].varPam->variable);
    EXPECT_EQ(*(int32_t *)ATUOptions[3].varPam->variable, 77);
}

TEST_F(ATUBoardTest, MenuChangesRefusedWhileTransmitting) {
    SyncATUMenuVariables();
    PutInTransmit();
    Step(0, true);
    Step(3, true);
    EXPECT_FALSE(ATUNetworkEngaged());
    EXPECT_EQ(GetATUInductors(), 0);
    // The shadow values revert to the hardware state
    EXPECT_FALSE(*(bool *)ATUOptions[0].varPam->variable);
    EXPECT_EQ(*(int32_t *)ATUOptions[3].varPam->variable, 0);
    ((void (*)(void))ATUOptions[5].func)();   // Bypass all is refused too
    PutInReceive();
}
