// Tests for the ATU circuit model (ATUModel.cpp), checked against the Python model
// in code/tools/atu_vna/model.py through golden vectors (export_golden.py).
// Built in all_ATU_tests, with ATU_PRESENT defined.

#include "gtest/gtest.h"

#include <cmath>
#include <cstdlib>
#include "../src/PhoenixSketch/SDT.h"
#include "../src/PhoenixSketch/ATUModel.h"
#include "atu_golden.h"

namespace {

// The cache is about 38 KB: keep it off the stack, as the firmware does
ATUModelCache cache;
ATUTopK topk;

atu_cf Z(float r, float x){ return {r, x}; }

void Prepare(double f_Hz){ ATUModelPrepare(&cache, &ATUModelDefaultParams, f_Hz); }

}  // namespace

TEST(ATUModel, MatchesPythonModelOnGoldenVectors) {
    const int n = sizeof(atuGolden) / sizeof(atuGolden[0]);
    ASSERT_GT(n, 400);
    double lastF = -1;
    float worst = 0;
    for (int i = 0; i < n; i++){
        const ATUGoldenCase &g = atuGolden[i];
        if (g.f_MHz != lastF){ Prepare(g.f_MHz * 1e6); lastF = g.f_MHz; }
        atu_cf got = ATUModelGamma(&cache, g.topo, g.l, g.c, Z(g.R, g.X));
        float err = hypotf(got.re - (float)g.gRe, got.im - (float)g.gIm);
        if (err > worst) worst = err;
        EXPECT_LT(err, 1e-3f) << "case " << i << ": topo " << g.topo << " L " << g.l
                              << " C " << g.c << " f " << g.f_MHz << " MHz, Z "
                              << g.R << (g.X < 0 ? "" : "+") << g.X << "j";
    }
    std::cout << "  worst |dGamma| over " << n << " cases: " << worst << std::endl;
}

TEST(ATUModel, EmptyNetworkIntoMatchedLoadIsNearlyMatched) {
    // Report section 3.1: the empty network's return loss is ~40 dB at 160 m
    Prepare(1.9e6);
    EXPECT_LT(ATUCAbs(ATUModelGamma(&cache, ATU_TOPO_LC, 0, 0, Z(50, 0))), 0.02f);
    // and about 10 dB on 6 m because of the path length
    Prepare(50.85e6);
    float g6 = ATUCAbs(ATUModelGamma(&cache, ATU_TOPO_LC, 0, 0, Z(50, 0)));
    EXPECT_GT(g6, 0.2f);
    EXPECT_LT(g6, 0.45f);
}

TEST(ATUModel, SearchFindsTheBruteForceMinimum) {
    const double freqs[] = {3.65e6, 14.2e6, 28.4e6};
    const atu_cf loads[] = {Z(150, 0), Z(16.7f, 0), Z(30, 40), Z(30, -40), Z(400, 300)};
    for (double f : freqs){
        Prepare(f);
        for (atu_cf zl : loads){
            float best = 2.0f;
            int bt = 0, bl = 0, bc = 0;
            for (int t = 0; t < ATU_NUM_TOPO; t++)
                for (int l = 0; l < ATU_NUM_CODES; l++)
                    for (int c = 0; c < ATU_NUM_CODES; c++){
                        float g = ATUCAbs(ATUModelGamma(&cache, t, l, c, zl));
                        if (g < best){ best = g; bt = t; bl = l; bc = c; }
                    }
            ATUCandidate got = ATUModelSearchAll(&cache, zl, &topk);
            EXPECT_NEAR(got.g, best, 1e-5f) << f << " Hz";
            EXPECT_EQ(got.topo, bt);
            EXPECT_EQ(got.l, bl);
            EXPECT_EQ(got.c, bc);
        }
    }
}

TEST(ATUModel, TopKIsSortedAndHoldsTheBest) {
    Prepare(7.1e6);
    ATUModelSearchAll(&cache, Z(30, 40), &topk);
    ASSERT_EQ(topk.n, ATU_TOPK);
    EXPECT_TRUE(topk.sorted);
    for (int i = 1; i < topk.n; i++) EXPECT_LE(topk.e[i - 1].g, topk.e[i].g);
    // Each entry's |Gamma| is the model's value for its setting
    for (int i = 0; i < topk.n; i += 37){
        const ATUCandidate &c = topk.e[i];
        EXPECT_NEAR(c.g, ATUCAbs(ATUModelGamma(&cache, c.topo, c.l, c.c, Z(30, 40))), 1e-6f);
    }
}

TEST(ATUModel, ChunkedSearchEqualsOneShot) {
    Prepare(21.2e6);
    ATUCandidate one = ATUModelSearchAll(&cache, Z(80, -120), &topk);
    static ATUTopK chunked;
    ATUTopKReset(&chunked);
    for (uint16_t u = 0; u < 512; u += 37)
        ATUModelSearchChunk(&cache, Z(80, -120), u, u + 37, &chunked);
    ATUTopKFinish(&chunked);
    EXPECT_EQ(chunked.e[0].topo, one.topo);
    EXPECT_EQ(chunked.e[0].l, one.l);
    EXPECT_EQ(chunked.e[0].c, one.c);
    EXPECT_FLOAT_EQ(chunked.e[ATU_TOPK - 1].g, topk.e[ATU_TOPK - 1].g);
}

TEST(ATUModel, MatchesReportFourToOneSettings) {
    // Report section 3.6: these settings turn 50 ohm on ANT into ~12.5 / ~200 ohm at TX.
    // Transforming the load the other way, a 12.5 / 200 ohm load is matched at TX.
    struct { int topo, l, c; double f; float r; float maxG; } cases[] = {
        {ATU_TOPO_LC, 0b00001110, 0b01011111, 3.65e6, 12.5f, 0.05f},  // model SWR 1.03
        {ATU_TOPO_CL, 0b00111111, 0b00011000, 3.65e6, 200.0f, 0.05f}, // model SWR 1.05
        {ATU_TOPO_LC, 0b00000001, 0b00010110, 14.28e6, 12.5f, 0.06f}, // model SWR 1.09
    };
    for (auto &k : cases){
        Prepare(k.f);
        // Zin with 50 ohm on ANT, compared with the target resistance
        atu_cf g50 = ATUModelGamma(&cache, k.topo, k.l, k.c, Z(50, 0));
        float zr = 50.0f * ((1 - g50.re) * (1 + g50.re) - g50.im * g50.im)
                   / ((1 - g50.re) * (1 - g50.re) + g50.im * g50.im);
        EXPECT_NEAR(zr, k.r, 0.15f * k.r);
    }
}

TEST(ATUModel, PerturbedModelsDifferSlightlyFromNominal) {
    Prepare(28.4e6);
    // L7 (bit 6) near its self resonance at 28 MHz is sensitive to a 2 % change
    atu_cf nom = ATUModelGamma(&cache, ATU_TOPO_LC, 0x40, 0x03, Z(50, 0));
    float maxDiff = 0;
    for (int s = 0; s < ATU_NUM_PERT; s++){
        atu_cf p = ATUModelGammaPerturbed(&cache, s, ATU_TOPO_LC, 0x40, 0x03, Z(50, 0));
        maxDiff = fmaxf(maxDiff, hypotf(p.re - nom.re, p.im - nom.im));
    }
    EXPECT_GT(maxDiff, 0.001f);   // 0.0039 with the generated table
    EXPECT_LT(maxDiff, 0.5f);
    // With no coil selected the perturbation has nothing to act on
    atu_cf e0 = ATUModelGamma(&cache, ATU_TOPO_CL, 0, 0x11, Z(20, 10));
    atu_cf e1 = ATUModelGammaPerturbed(&cache, 3, ATU_TOPO_CL, 0, 0x11, Z(20, 10));
    EXPECT_NEAR(e0.re, e1.re, 1e-5f);
    EXPECT_NEAR(e0.im, e1.im, 1e-5f);
}

TEST(ATUModel, RobustPickIsDeterministicAndNoWorseThanItsCandidates) {
    Prepare(28.4e6);
    atu_cf zl = Z(150, 0);
    ATUModelSearchAll(&cache, zl, &topk);
    ATURobustChoice a = ATUModelRobustPick(&cache, zl, &topk);
    ATURobustChoice b = ATUModelRobustPick(&cache, zl, &topk);
    EXPECT_EQ(a.best.topo, b.best.topo);
    EXPECT_EQ(a.best.l, b.best.l);
    EXPECT_EQ(a.best.c, b.best.c);
    EXPECT_GE(a.worst, a.best.g);
    // Its worst case is no worse than the plain best setting's worst case
    const ATUCandidate &p = topk.e[0];
    float plainWorst = p.g;
    for (int s = 0; s < ATU_NUM_PERT; s++)
        plainWorst = fmaxf(plainWorst, ATUCAbs(ATUModelGammaPerturbed(&cache, s, p.topo, p.l, p.c, zl)));
    EXPECT_LE(a.worst, plainWorst + 1e-6f);
    // A good match is still available on 10 m (report: robust median SWR ~1.09)
    EXPECT_LT(a.worst, 0.15f);
}

// ================== CAT: AX1 benchmark ==================

extern char *command_parser(char *command);

TEST(ATUModel, CatAX1RunsTheBenchmark) {
    InitializeRFHardware();
    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    UpdateRFHardwareState();
    char cmd[] = "AX1;";
    std::string reply(command_parser(cmd));
    // AX1,<f_kHz>,<prepare_us>,<search_us>,<robust_us>,<topo><LLL><CCC>,<g x 1000>;
    EXPECT_EQ(reply.substr(0, 4), "AX1,");
    EXPECT_EQ(reply.back(), ';');
    int commas = 0;
    for (char ch : reply) commas += (ch == ',');
    EXPECT_EQ(commas, 6);
    char bad[] = "AX2;";
    EXPECT_STREQ(command_parser(bad), "?;");
    char rd[] = "AX;";
    EXPECT_STREQ(command_parser(rd), "?;");
}
