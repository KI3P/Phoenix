/* 
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the 
terms of the GNU General Public License as published by the Free Software 
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix. 
If not, see <https://www.gnu.org/licenses/>.
*/

/**
 * @file DSP_FIR.cpp
 * @brief This file contains the definitions for DSP coefficients and some functions that calculate coefficients
 * 
 */

#include "SDT.h"

static char *dspfirfilename = nullptr;

// ============================================================================
//  Filter tables generated at run time
//
//  These used to be coefficient tables designed offline for one audio sample
//  rate (24 ksps: 192 ksps at the ADC, decimated by 8). Running the radio at
//  any other rate scaled every corner and centre frequency by the ratio of the
//  rates, so at 176.4 ksps a filter labelled 2.0 kHz actually cut at 1.84 kHz.
//
//  They are now regenerated from an analog design spec on every sample rate
//  change, which puts each response back on its labelled frequency at any rate.
//  See InitializeReceiveAudioFilterCoeffs() and InitializeTransmitFilterCoeffs()
//  at the bottom of this file.
//
//  The original tables are kept verbatim in code/test/reference_filters.cpp so
//  the test suite can measure the generated filters against them.
// ============================================================================

// CW audio lowpass filters: 12 pole Chebyshev type I, 0.02 dB ripple.
// 6 biquad sections of {b0, b1, b2, -a1, -a2}.
float32_t CW_AudioFilterCoeffs1[30] = {0};  /** CW audio lowpass, 840 Hz nominal */
float32_t CW_AudioFilterCoeffs2[30] = {0};  /** CW audio lowpass, 1080 Hz nominal */
float32_t CW_AudioFilterCoeffs3[30] = {0};  /** CW audio lowpass, 1320 Hz nominal */
float32_t CW_AudioFilterCoeffs4[30] = {0};  /** CW audio lowpass, 1800 Hz nominal */
float32_t CW_AudioFilterCoeffs5[30] = {0};  /** CW audio lowpass, 2000 Hz nominal */

/** CW decoder input lowpass, 64 tap Kaiser windowed sinc */
float32_t CW_Filter_Coeffs2[64] = {0};

//=================== Excite Coefficients ============
//48 Tap Kaiser 192KHz 8HKZ Fc RXfilters Dec and Interpolation
float32_t coeffs192K_10K_LPF_FIR[48] = {
  -9.489855110236549150E-6,
  162.0562443716462440E-6,
  336.4923361276530040E-6,
  670.1825668306562420E-6,
  0.001118525968862066,
  0.001579147826767285,
  0.001852971855408917,
  0.001654834862116168,
  672.0062171704363440E-6,
  -0.001330388414656256,
  -0.004370672204695505,
  -0.008116214105266742,
  -0.011801558382590140,
  -0.014244430060197440,
  -0.013991286573630231,
  -0.009591314629104829,
  55.15979573749079630E-6,
  0.015330868054737832,
  0.035648433279783398,
  0.059353733010970522,
  0.083890755684744231,
  0.106176326681328731,
  0.123138113118064343,
  0.132306356993624447,
  0.132306356993624447,
  0.123138113118064343,
  0.106176326681328731,
  0.083890755684744231,
  0.059353733010970522,
  0.035648433279783398,
  0.015330868054737832,
  55.15979573749079630E-6,
  -0.009591314629104829,
  -0.013991286573630231,
  -0.014244430060197440,
  -0.011801558382590140,
  -0.008116214105266742,
  -0.004370672204695505,
  -0.001330388414656256,
  672.0062171704363440E-6,
  0.001654834862116168,
  0.001852971855408917,
  0.001579147826767285,
  0.001118525968862066,
  670.1825668306562420E-6,
  336.4923361276530040E-6,
  162.0562443716462440E-6,
  -9.489855110236549150E-6
};
float32_t coeffs48K_8K_LPF_FIR[48] = {
  42.07251256297374200E-6,
  -140.7585461814297220E-6,
  -474.6573692370658360E-6,
  -910.1877217000583190E-6,
  -0.001002380560950271,
  -214.0330069096950180E-6,
  0.001476493526486391,
  0.003032917910844886,
  0.002651184249675911,
  -799.0261189923908200E-6,
  -0.006003327154821903,
  -0.008881895242516839,
  -0.005014510701112499,
  0.006071336723217765,
  0.018084892184059984,
  0.020179984097194015,
  0.004666151232929589,
  -0.024269532666953794,
  -0.047977714295749374,
  -0.041374408930560248,
  0.011283003090050767,
  0.102653022828769605,
  0.201046155814701449,
  0.264813622122047398,
  0.264813622122047398,
  0.201046155814701449,
  0.102653022828769605,
  0.011283003090050767,
  -0.041374408930560248,
  -0.047977714295749374,
  -0.024269532666953794,
  0.004666151232929589,
  0.020179984097194015,
  0.018084892184059984,
  0.006071336723217765,
  -0.005014510701112499,
  -0.008881895242516839,
  -0.006003327154821903,
  -799.0261189923908200E-6,
  0.002651184249675911,
  0.003032917910844886,
  0.001476493526486391,
  -214.0330069096950180E-6,
  -0.001002380560950271,
  -910.1877217000583190E-6,
  -474.6573692370658360E-6,
  -140.7585461814297220E-6,
  42.07251256297374200E-6
};
/** Transmit decimate-by-2, 24k -> 12k, 48 tap Kaiser windowed sinc */
float32_t coeffs12K_8K_LPF_FIR[48] = {0};

/** Transmit interpolate-by-2, 12k -> 24k. This is the stage that sets the
 *  transmit audio bandwidth, so it is anchored in Hz rather than left to scale
 *  with the sample rate. 48 tap Kaiser windowed sinc. */
float32_t FIR_int3_12ksps_48tap_2k7[48] = {0};

// Audio equaliser cells: 4 stagger-tuned bandpass biquads each, peak normalised
// to unity. Centres run 198.425 Hz to 4000 Hz, roughly 1/3 octave apart.
float32_t EQ_Band1Coeffs[20] = {0};
float32_t EQ_Band2Coeffs[20] = {0};
float32_t EQ_Band3Coeffs[20] = {0};
float32_t EQ_Band4Coeffs[20] = {0};
float32_t EQ_Band5Coeffs[20] = {0};
float32_t EQ_Band6Coeffs[20] = {0};
float32_t EQ_Band7Coeffs[20] = {0};
float32_t EQ_Band8Coeffs[20] = {0};
float32_t EQ_Band9Coeffs[20] = {0};
float32_t EQ_Band10Coeffs[20] = {0};
float32_t EQ_Band11Coeffs[20] = {0};
float32_t EQ_Band12Coeffs[20] = {0};
float32_t EQ_Band13Coeffs[20] = {0};
float32_t EQ_Band14Coeffs[20] = {0};
// Concatenate all the EQ band filter coefficients so we can loop over them
float32_t (*EQ_Coeffs[14])[20] = { &EQ_Band1Coeffs, &EQ_Band2Coeffs, &EQ_Band3Coeffs, &EQ_Band4Coeffs,
                               &EQ_Band5Coeffs, &EQ_Band6Coeffs, &EQ_Band7Coeffs, &EQ_Band8Coeffs,
                              &EQ_Band9Coeffs, &EQ_Band10Coeffs, &EQ_Band11Coeffs, &EQ_Band12Coeffs,
                              &EQ_Band13Coeffs, &EQ_Band14Coeffs};

//Hilbert Coefficients
float32_t FIR_Hilbert_coeffs_45[100] = // 278 576  12K
{ 0.000198724236183671,
 0.000254683831862001,
 0.000315458144123207,
 0.000368610207103309,
 0.000465506783939857,
 0.000511928039934757,
 0.000647299422206719,
 0.000698074642452268,
 0.000854171713729817,
 0.000945871414257390,
 0.001076408944312920,
 0.001275920792277492,
 0.001306915989978013,
 0.001703306484332476,
 0.001550515101469794,
 0.002227298340949841,
 0.001835463144518919,
 0.002820293580739281,
 0.002224274854022750,
 0.003419621857599474,
 0.002819842043365285,
 0.003926578130419233,
 0.003762668283074692,
 0.004216679139836836,
 0.005216147587106480,
 0.004163484463570267,
 0.007339230162553567,
 0.003675607102120641,
 0.010249150284403673,
 0.002743452714859422,
 0.013980372783435091,
 0.001489894753953287,
 0.018448562889854227,
 0.000219023228827659,
 0.023429266754326966,
-0.000538612578294005,
 0.028559517413246797,
 0.000026682439276827,
 0.033366738095610159,
 0.003106342852277801,
 0.037323748655479140,
 0.010557712182430569,
 0.039922600621868351,
 0.025820858117389513,
 0.040754864597457946,
 0.057491275979955001,
 0.039583257572973069,
 0.140431345936612995,
 0.036390015738706309,
 0.865063715035993996,
 0.031391300797281890,
-0.327903573861813014,
 0.025013429083301980,
-0.148898771042744293,
 0.017834389903486098,
-0.094047715083500544,
 0.010501189429832865,
-0.064304366997204127,
 0.003638357049891278,
-0.044513820587759587,
-0.002235624940045236,
-0.030331681178903912,
-0.006769427152121139,
-0.020066155186087276,
-0.009808208167348372,
-0.012834161617157508,
-0.011388784109210482,
-0.008006187994283959,
-0.011705147912663404,
-0.005030666033447775,
-0.011054188692668034,
-0.003395620019522265,
-0.009774147564620667,
-0.002641217575854609,
-0.008188042301640656,
-0.002384731896854369,
-0.006561415369602986,
-0.002339199541964768,
-0.005079300499545814,
-0.002318061252603545,
-0.003842514275124937,
-0.002225239010723332,
-0.002879421464156411,
-0.002034553450037827,
-0.002166978296109626,
-0.001764401144908235,
-0.001654383125037512,
-0.001453535333182090,
-0.001283804403268615,
-0.001142233712330263,
-0.001004797547016575,
-0.000860873307921386,
-0.000781417508646928,
-0.000625726502729441,
-0.000593026532750594,
-0.000440206606932182,
-0.000430981422416596,
-0.000299086415167579,
-0.000293666182366421,
-0.000193345055588135

};

float32_t FIR_Hilbert_coeffs_neg_45[100] =   //Exite Hilbert transforms for 12K SPS BW 5400 100 taps
{ -0.000104828886628755,
-0.000117719664105387,
-0.000179320525624148,
-0.000205669060801986,
-0.000270286475558104,
-0.000332604968214234,
-0.000386819028473551,
-0.000493164616992682,
-0.000552187708486295,
-0.000674078778415017,
-0.000787897902733834,
-0.000880670533595121,
-0.001080992707313235,
-0.001168160302446560,
-0.001365286530613688,
-0.001636783384052048,
-0.001559963634144742,
-0.002356313266829644,
-0.001678217635730416,
-0.003240524231127448,
-0.001945858691780062,
-0.003970012170510779,
-0.002804622726618263,
-0.004094681623970159,
-0.004691992111496490,
-0.003369737266986447,
-0.007627097490107733,
-0.002195191592412418,
-0.010835260483481341,
-0.001845290685708202,
-0.012766336781617968,
-0.004153071025321154,
-0.011756607386998292,
-0.010556794294806793,
-0.007228189577834320,
-0.020846529514028617,
-0.000887005875166711,
-0.032318023634858514,
 0.002824775637078142,
-0.040051810693587799,
-0.002513661566225675,
-0.038562584732477104,
-0.023692311071245833,
-0.024291462351993202,
-0.067204321144781837,
 0.002221169819425520,
-0.145832963504399560,
 0.035705388989502325,
-0.352113137784848706,
 0.067646258294101688,
 0.819224519212560343,
 0.089389932910583753,
 0.083898046820836417,
 0.095487826549608398,
 0.005097439529971089,
 0.085709969555230026,
-0.010424760352139743,
 0.064811294048651949,
-0.003898907826858565,
 0.040265585480685734,
 0.008749537601820631,
 0.019155494269048438,
 0.018678233958939739,
 0.005700076331359550,
 0.022203747632268241,
 0.000410110361091821,
 0.019752962364774186,
 0.000933775296421688,
 0.014070217079109496,
 0.003844223553408773,
 0.008207357923911553,
 0.006337891834889198,
 0.004112629333540725,
 0.007109267624208683,
 0.002222779781756527,
 0.006264069949833283,
 0.001893005138071308,
 0.004640304124501774,
 0.002154680835501082,
 0.003076655796887437,
 0.002307279081475436,
 0.002003764398968816,
 0.002117456102702203,
 0.001427355620025981,
 0.001691149314908232,
 0.001139213958168955,
 0.001232148667634720,
 0.000938062422309971,
 0.000871368830893603,
 0.000732817577251988,
 0.000628578254366384,
 0.000526829579234758,
 0.000462087337619918,
 0.000353466739325737,
 0.000329895504555281,
 0.000229266229860308,
 0.000216471955570351,
 0.000146341941125966,
 0.000125349498762248,
 0.000088059239423163
};

//===================End Excite Coefficients ============

float32_t* mag_coeffs[11] =
{
  // for Index 0 [1xZoom == no zoom] the mag_coeffs will consist of  a NULL  ptr,
  // since the filter is not going to be used in this  mode!
  (float32_t*)NULL,
  (float32_t*)(const float32_t[]) {
    // 2x magnify - index 1
    // 12kHz, sample rate 48k, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
    0.228454526413293696,
    0.077639329099949764,
    0.228454526413293696,
    0.635534925142242080,
    -0.170083307068779194,

    0.436788292542003964,
    0.232307972937606161,
    0.436788292542003964,
    0.365885230717786780,
    -0.471769788739400842,

    0.535974654742658707,
    0.557035600464780845,
    0.535974654742658707,
    0.125740787233286133,
    -0.754725697183384336,

    0.501116342273565607,
    0.914877831284765408,
    0.501116342273565607,
    0.013862536615004284,
    -0.930973052446900984
  },

  (float32_t*)(const float32_t[]) {
    // 4x magnify - index 2
    // 6kHz, sample rate 48k, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
    0.182208761527446556,
    -0.222492493114674145,
    0.182208761527446556,
    1.326111070880959810,
    -0.468036100821178802,

    0.337123762652097259,
    -0.366352718812586853,
    0.337123762652097259,
    1.337053579516321200,
    -0.644948386007929031,

    0.336163175380826074,
    -0.199246162162897811,
    0.336163175380826074,
    1.354952684569386670,
    -0.828032873168141115,

    0.178588201750411041,
    0.207271695028067304,
    0.178588201750411041,
    1.386486967455699220,
    -0.950935065984588657
  },

  (float32_t*)(const float32_t[]) {
    // 8x magnify - index 3
    // 3kHz, sample rate 48k, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
    0.185643392652478922,
    -0.332064345389014803,
    0.185643392652478922,
    1.654637402827731090,
    -0.693859842743674182,

    0.327519300813245984,
    -0.571358085216950418,
    0.327519300813245984,
    1.715375037176782860,
    -0.799055553586324407,

    0.283656142708241688,
    -0.441088976843048652,
    0.283656142708241688,
    1.778230635987093860,
    -0.904453944560528522,

    0.079685368654848945,
    -0.011231810140649204,
    0.079685368654848945,
    1.825046003243238070,
    -0.973184930412286708
  },

  (float32_t*)(const float32_t[]) {
    // 16x magnify - index 4
    // 1k5, sample rate 48k, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
    0.194769868656866380,
    -0.379098413160710079,
    0.194769868656866380,
    1.824436402073870810,
    -0.834877726226893380,

    0.333973874901496770,
    -0.646106479315673776,
    0.333973874901496770,
    1.871892825636887640,
    -0.893734096124207178,

    0.272903880596429671,
    -0.513507745397738469,
    0.272903880596429671,
    1.918161772571113750,
    -0.950461788366234739,

    0.053535383722369843,
    -0.069683422367188122,
    0.053535383722369843,
    1.948900719896301760,
    -0.986288064973853129
  },

  (float32_t*)(const float32_t[]) {
    // 32x magnify - index 5
    // 750Hz, sample rate 48k, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
    0.201507402588557594,
    -0.400273615727755550,
    0.201507402588557594,
    1.910767558906650840,
    -0.913508748356010480,

    0.340295203367131205,
    -0.674930558961690075,
    0.340295203367131205,
    1.939398230905991390,
    -0.945058078678563840,

    0.271859921641011359,
    -0.535453706265515361,
    0.271859921641011359,
    1.966439529620203740,
    -0.974705666636711099,

    0.047026497485465592,
    -0.084562104085501480,
    0.047026497485465592,
    1.983564238653704900,
    -0.993055129539134551
  },

  (float32_t*)(const float32_t[]) {
    // 64x magnify - index 6
    // 374Hz, sr 48k, 0.02dB ripple, 60dB stopband elliptic
    // DD4WH, 2018_03_24

    0.241056639221550989,
    -0.481274384783607956,
    0.241056639221550989,
    1.949355134029925550,
    -0.950194027689419740,

    0.348059943588306275,
    -0.694622621265274853,
    0.348059943588306275,
    1.966699951543778860,
    -0.968197217455116443,

    0.259592008997311219,
    -0.517100588623714774,
    0.259592008997311219,
    1.983085371558495740,
    -0.985168800929403399,

    0.042223607998797694,
    -0.082088490093798844,
    0.042223607998797694,
    1.993523066505831660,
    -0.995881792409628042
  },

  (float32_t*)(const float32_t[]) {
    // 128x magnify - index 7
    // 187Hz, sample rate 48k, ripple 0.02dB, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH 2018_03_24
    0.243976032331821663,
    -0.487739726489511083,
    0.243976032331821663,
    1.974570407912224380,
    -0.974782746086356844,

    0.350666090990641666,
    -0.700954871622642472,
    0.350666090990641666,
    1.983591708136026810,
    -0.983969018494667669,

    0.260268176176534360,
    -0.520013508234821287,
    0.260268176176534360,
    1.992032152306574270,
    -0.992554996424821700,

    0.041842895868125313,
    -0.083095418270055094,
    0.041842895868125313,
    1.997347796837673830,
    -0.997938170303869221
  },

  // TODO: calculate new coeffs!
  (float32_t*)(const float32_t[]) {
    // 256x magnify - index 8
    // 187Hz, sample rate 48k, ripple 0.02dB, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH 2018_03_24
    0.243976032331821663,
    -0.487739726489511083,
    0.243976032331821663,
    1.974570407912224380,
    -0.974782746086356844,

    0.350666090990641666,
    -0.700954871622642472,
    0.350666090990641666,
    1.983591708136026810,
    -0.983969018494667669,

    0.260268176176534360,
    -0.520013508234821287,
    0.260268176176534360,
    1.992032152306574270,
    -0.992554996424821700,

    0.041842895868125313,
    -0.083095418270055094,
    0.041842895868125313,
    1.997347796837673830,
    -0.997938170303869221
  },

  // TODO: calculate new coeffs!
  (float32_t*)(const float32_t[]) {
    // 512x magnify - index 9
    // 187Hz, sample rate 48k, ripple 0.02dB, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH 2018_03_24
    0.243976032331821663,
    -0.487739726489511083,
    0.243976032331821663,
    1.974570407912224380,
    -0.974782746086356844,

    0.350666090990641666,
    -0.700954871622642472,
    0.350666090990641666,
    1.983591708136026810,
    -0.983969018494667669,

    0.260268176176534360,
    -0.520013508234821287,
    0.260268176176534360,
    1.992032152306574270,
    -0.992554996424821700,

    0.041842895868125313,
    -0.083095418270055094,
    0.041842895868125313,
    1.997347796837673830,
    -0.997938170303869221
  },

  (float32_t*)(const float32_t[]) {
    // 1024x magnify - index 10
    // 187Hz, sample rate 48k, ripple 0.02dB, 60dB stopband, elliptic
    // a1 and a2 negated! order: b0, b1, b2, a1, a2
    // Iowa Hills IIR Filter Designer, DD4WH 2018_03_24
    0.243976032331821663,
    -0.487739726489511083,
    0.243976032331821663,
    1.974570407912224380,
    -0.974782746086356844,

    0.350666090990641666,
    -0.700954871622642472,
    0.350666090990641666,
    1.983591708136026810,
    -0.983969018494667669,

    0.260268176176534360,
    -0.520013508234821287,
    0.260268176176534360,
    1.992032152306574270,
    -0.992554996424821700,

    0.041842895868125313,
    -0.083095418270055094,
    0.041842895868125313,
    1.997347796837673830,
    -0.997938170303869221
  }
};




/**
 * Calculate sinc function
 * 
 * @param m
 * @param fc
 */
float MSinc(int m, float fc) {
  float x = m * HALF_PI;
  if (m == 0)
    return 1.0f;
  else
    return arm_sin_f32(x * fc) / (fc * x);
}

/**
 * Izero
 */
float32_t Izero(float32_t x) {
  float32_t x2 = x / 2.0;
  float32_t summe = 1.0;
  float32_t ds = 1.0;
  float32_t di = 1.0;
  float32_t errorlimit = 1e-9;
  float32_t tmp;

  do {
    tmp = x2 / di;
    tmp *= tmp;
    ds *= tmp;
    summe += ds;
    di += 1.0;
  } while (ds >= errorlimit * summe);
  return (summe);
}

/**
 * Calcululate new FIR filter coefficients. 
 * 
 * @param *coeffs_I pointer to coefficients variable
 * @param numCoeffs no. of coefficients to calculate
 * @param fc_Hz frequency where it happens
 * @param Astop_dB stopband attenuation in dB
 * @param type Type of filter
 * @param dfc_Hz half-filter bandwidth (only for bandpass and notch)
 * @param Fsamprate_Hz sample rate
 */
void CalcFIRCoeffs(float *coeffs_I, int numCoeffs, float32_t fc_Hz, float32_t Astop_dB, 
                  FilterType type, float dfc_Hz, float Fsamprate_Hz)
{ 
  // Wheatley, M. (2011): CuteSDR Technical Manual. www.metronix.com, pages 118 - 120, FIR with Kaiser-Bessel Window
  // assess required number of coefficients by
  //     numCoeffs = (Astop - 8.0) / (2.285 * TPI * normFtrans);
  // selecting high-pass, numCoeffs is forced to an even number for better frequency response

  int nc    = numCoeffs;
  float32_t Beta;
  float32_t izb;
  float fcf = fc_Hz;
  float x, w;
  float fc, dfc;
  fc        = fc_Hz / Fsamprate_Hz;
  dfc       = dfc_Hz / Fsamprate_Hz;
  uint16_t n_dec_taps = numCoeffs;
  // calculate Kaiser-Bessel window shape factor beta from stop-band attenuation
  if (Astop_dB < 20.96) {
    Beta = 0.0;
  } else {
    if (Astop_dB >= 50.0) {
      Beta = 0.1102 * (Astop_dB - 8.71);
    } else {
      Beta = 0.5842 * powf((Astop_dB - 20.96), 0.4) + 0.07886 * (Astop_dB - 20.96);
    }
  }
  memset(coeffs_I, 0.0, sizeof(float32_t)*n_dec_taps);    //zero entire buffer, important for variables from DMAMEM

  izb = Izero(Beta);
  if (type == Lowpass) { // low pass filter
    fcf = fc * 2.0;
    nc  =  numCoeffs;
  } else if (type == Highpass) { // high-pass filter
    fcf = -fc;
    nc  =  2 * (numCoeffs / 2);
  } else if (type == Bandpass ) { // band-pass filter
    fcf = dfc;
    nc  =  2 * (numCoeffs / 2); // maybe not needed
  } else if (type == Hilbert) { // Hilbert transform
    nc  =  2 * (numCoeffs / 2);
    // clear coefficients
    for (int ii = 0; ii < 2 * (nc - 1); ii++) {
      coeffs_I[ii] = 0;
    }
    coeffs_I[nc] = 1;                                   // set real delay
    for (int ii = 1; ii < (nc + 1); ii += 2) {          // set imaginary Hilbert coefficients
      if (2 * ii == nc)
        continue;
      x = (float)(2 * ii - nc) / (float)nc;
      w = Izero(Beta * sqrtf(1.0f - x * x)) / izb; // Kaiser window
      coeffs_I[2 * ii + 1] = 1.0f / (HALF_PI * (float)(ii - nc / 2)) * w ;
    }
    return;
  }

  for (int ii = - nc, jj = 0; ii < nc; ii += 2, jj++) {
    x = (float)ii / (float)nc;
    w = Izero(Beta * sqrtf(1.0f - x * x)) / izb; // Kaiser window
    coeffs_I[jj] = fcf * MSinc(ii, fcf) * w;
  }

  if (type == Highpass) {
    coeffs_I[nc / 2] += 1;
  } else if (type == 2) {
    for (int jj = 0; jj < nc + 1; jj++)
      coeffs_I[jj] *= 2.0f * cosf(HALF_PI * (2 * jj - nc) * fc);
  } else if (type == 3) {
    for (int jj = 0; jj < nc + 1; jj++)
      coeffs_I[jj] *= -2.0f * cosf(HALF_PI * (2 * jj - nc) * fc);
    coeffs_I[nc / 2] += 1;
  }

}

/**
 * Calculate complex FIR coefficients
 * 
 * @param *coeffs_I
 * @param *coeffs_Q
 * @param numCoeffs
 * @param FLoCut_Hz
 * @param FHiCut_Hz High cutoff frequency, must be greater than FLoCut
 * @param SampleRate_Hz
 */
void CalcCplxFIRCoeffs(float * coeffs_I, float * coeffs_Q, int numCoeffs, 
                    float32_t FLoCut, float32_t FHiCut, float SampleRate)
{
  //calculate some normalized filter parameters
  float32_t nFL = FLoCut / SampleRate;
  float32_t nFH = FHiCut / SampleRate;
  float32_t nFc = (nFH - nFL) / 2.0; //prototype LP filter cutoff
  float32_t nFs = PI * (nFH + nFL); //2 PI times required frequency shift (FHiCut+FLoCut)/2
  float32_t fCenter = 0.5 * (float32_t)(numCoeffs - 1); //floating point center index of FIR filter
  float32_t x;
  float32_t z;
  uint16_t n_dec_taps = numCoeffs;
  memset(coeffs_I, 0.0, sizeof(float32_t)*n_dec_taps);    //zero entire buffer, important for variables from DMAMEM
  memset(coeffs_Q, 0.0, sizeof(float32_t)*n_dec_taps);

  //create LP FIR windowed sinc, sin(x)/x complex LP filter coefficients
  for (int i = 0; i < numCoeffs; i++)  {
    x = (float32_t)i - fCenter;
    if ( abs((float)i - fCenter) < 0.01) //deal with odd size filter singularity where sin(0)/0==1
      z = 2.0 * nFc;
    else
      switch (FIR_FILTER_WINDOW) {
        case 1:    // 4-term Blackman-Harris --> this is what Power SDR uses
          z = (float32_t)sinf(TWO_PI * x * nFc) / (PI * x) *
              (0.35875 - 0.48829 * cosf( (TWO_PI * i) / (numCoeffs - 1) )
               + 0.14128 * cosf( (FOUR_PI * i) / (numCoeffs - 1) )
               - 0.01168 * cosf( (SIX_PI * i) / (numCoeffs - 1) ) );
          break;

        case 2:
          z = (float32_t)sinf(TWO_PI * x * nFc) / (PI * x) *
              (0.355768 - 0.487396 * cosf( (TWO_PI * i) / (numCoeffs - 1) )
               + 0.144232 * cosf( (FOUR_PI * i) / (numCoeffs - 1) )
               - 0.012604 * cosf( (SIX_PI * i) / (numCoeffs - 1) ) );
          break;

        case 3: // cosine
          z = (float32_t)sinf(TWO_PI * x * nFc) / (PI * x) *
              cosf((PI * (float32_t)i) / (numCoeffs - 1));
          break;

        case 4: // Hann
          z = (float32_t)sinf(TWO_PI * x * nFc) / (PI * x) *
              0.5 * (float32_t)(1.0 - (cosf(PI * 2 * (float32_t)i / (float32_t)(numCoeffs - 1))));
          break;
        default: // Blackman-Nuttall window
          z = (float32_t)sinf(TWO_PI * x * nFc) / (PI * x) *
              (0.3635819
               - 0.4891775 * cosf( (TWO_PI * i) / (numCoeffs - 1) )
               + 0.1365995 * cosf( (FOUR_PI * i) / (numCoeffs - 1) )
               - 0.0106411 * cosf( (SIX_PI * i) / (numCoeffs - 1) ) );
          break;
      }
    //shift lowpass filter coefficients in frequency by (hicut+lowcut)/2 to form bandpass filter anywhere in range
    coeffs_I[i]   = z * cosf(nFs * x);
    coeffs_Q[i]   = z * sinf(nFs * x);
  }
}

/**
 * Calculate the coefficients for a biquad IIR filter
 * 
 * @param *coefficient_set Pointer to the array to place the coefficients in. Must be length 5.
 * @param f0_Hz Center frequency / corner frequency
 * @param Q Quality factor
 * @param sample_rate_Hz Sample rate for the digital samples
 * @param filter_type Type of filter
 */
void SetIIRCoeffs(float32_t *coefficient_set, float32_t f0, float32_t Q, float32_t sample_rate, FilterType filter_type)
{
  /*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    Cascaded biquad (notch, peak, lowShelf, highShelf) [DD4WH, april 2016]
    ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
  // DSP Audio-EQ-cookbook for generating the coeffs of the RXfilters on the fly
  // www.musicdsp.org/files/Audio-EQ-Cookbook.txt  [by Robert Bristow-Johnson]
  // https://www.w3.org/2011/audio/audio-eq-cookbook.html
  // the ARM algorithm assumes the biquad form
  // y[n] = b0 * x[n] + b1 * x[n-1] + b2 * x[n-2] + a1 * y[n-1] + a2 * y[n-2]
  //
  // However, the cookbook formulae by Robert Bristow-Johnson AND the Iowa Hills IIR Filter designer
  // use this formula:
  //
  // y[n] = b0 * x[n] + b1 * x[n-1] + b2 * x[n-2] - a1 * y[n-1] - a2 * y[n-2]
  //
  // Therefore, we have to use negated a1 and a2 for use with the ARM function
  if (f0 > sample_rate / 2.0) f0 = sample_rate / 2.0;
  float32_t w0 = f0 * (TWO_PI / sample_rate);
  float32_t sinW0 = arm_sin_f32(w0);
  float32_t alpha = sinW0 / (Q * 2.0);
  float32_t cosW0 = arm_cos_f32(w0);
  float32_t scale = 1.0 / (1.0 + alpha);

  if (filter_type == Lowpass) { // lowpass coeffs

    coefficient_set[0] = ((1.0 - cosW0) / 2.0) * scale;   /* b0 */
    coefficient_set[1] = (1.0 - cosW0) * scale;           /* b1 */
    coefficient_set[2] = coefficient_set[0];              /* b2 */
    coefficient_set[3] = (2.0 * cosW0) * scale;           // negated    a1
    coefficient_set[4] = (-1.0 + alpha) * scale;          // negated    a2
  } else if (filter_type == Notch) {   // notch
    coefficient_set[0] =  1.0;                            /* b0 */
    coefficient_set[1] =  - 2.0 * cosW0;                  /* b1 */
    coefficient_set[2] =  1.0;                            /* b2 */
    coefficient_set[3] =  2.0 * cosW0 * scale;            // negated    a1
    coefficient_set[4] =  alpha - 1.0;                    // negated    a2
  }
}

/**
 * Bilinear transform of one second-order analog section into an ARM biquad.
 *
 * Maps a section with resonance wn (rad/s) and quality factor Q into the five
 * coefficients arm_biquad_cascade_df2T_f32 expects, {b0, b1, b2, -a1, -a2}.
 * The caller supplies the analog numerator; the denominator is always
 * s^2 + (wn/Q)s + wn^2.
 *
 * Substituting s = 2*Fs*(z-1)/(z+1) and clearing denominators gives a common
 * divisor of d = K^2 + K*wn/Q + wn^2 with K = 2*Fs, which is factored out here.
 *
 * @param coeffs Destination, 5 floats
 * @param b_s2 Analog numerator coefficient of s^2
 * @param b_s1 Analog numerator coefficient of s
 * @param b_s0 Analog numerator constant term
 * @param wn Section resonant frequency in rad/s
 * @param Q Section quality factor
 * @param Fs_Hz Sample rate the digital filter will run at
 */
static void BilinearBiquad(float32_t *coeffs, float32_t b_s2, float32_t b_s1, float32_t b_s0,
                           float32_t wn, float32_t Q, float32_t Fs_Hz) {
    const float32_t K = 2.0f * Fs_Hz;
    const float32_t K2 = K * K;
    const float32_t wn2 = wn * wn;
    const float32_t damping = K * wn / Q;
    const float32_t d = K2 + damping + wn2;

    coeffs[0] = (b_s2 * K2 + b_s1 * K + b_s0) / d;              /* b0 */
    coeffs[1] = (2.0f * b_s0 - 2.0f * b_s2 * K2) / d;           /* b1 */
    coeffs[2] = (b_s2 * K2 - b_s1 * K + b_s0) / d;              /* b2 */
    // The ARM biquad adds the feedback terms rather than subtracting them, so
    // a1 and a2 are stored negated.
    coeffs[3] = -(2.0f * (wn2 - K2)) / d;                       // negated a1
    coeffs[4] = -(K2 - damping + wn2) / d;                      // negated a2
}

/**
 * Prewarp a frequency for the bilinear transform.
 *
 * The bilinear transform compresses the analog frequency axis as it wraps it
 * onto the unit circle, so an analog section placed at f lands slightly below f
 * once discretised. Designing at the prewarped frequency instead cancels that,
 * putting the digital feature exactly on f at whatever sample rate is in use.
 * This is what makes the generated filters sample-rate independent.
 *
 * @param f_Hz Frequency the digital filter should land on
 * @param Fs_Hz Sample rate
 * @return Equivalent analog frequency in rad/s
 */
static float32_t PrewarpRadians(float32_t f_Hz, float32_t Fs_Hz) {
    return 2.0f * Fs_Hz * tanf(PI * f_Hz / Fs_Hz);
}

/**
 * Design a Chebyshev type I lowpass as a cascade of ARM biquads.
 *
 * The poles of a Chebyshev type I filter lie on an ellipse in the s-plane, at
 * positions given in closed form by the ripple and the order, so no iterative
 * design is needed. Each conjugate pair becomes one biquad section.
 *
 * The cascade is normalised to unity gain at DC. An even-order Chebyshev sits
 * at -ripple at DC and rises to 0 dB across the passband; the frozen tables
 * this replaces were normalised the same way, with DC at 0 dB and the ripple
 * above it.
 *
 * Note fRippleEdge_Hz is the edge of the equiripple region, which is the
 * natural parameter for this family - not the -3 dB or -6 dB corner. It sits a
 * few percent below the nominal cutoff the filter is labelled with.
 *
 * @param coeffs Destination, order/2 sections of 5 floats
 * @param order Filter order, must be even
 * @param ripple_dB Peak-to-peak passband ripple
 * @param fRippleEdge_Hz Frequency at which the passband ripple ends
 * @param Fs_Hz Sample rate the filter will run at
 */
void CalcChebyshevILowpassCoeffs(float32_t *coeffs, uint32_t order, float32_t ripple_dB,
                                 float32_t fRippleEdge_Hz, float32_t Fs_Hz) {
    const uint32_t nSections = order / 2;
    if (nSections == 0) return;
    // Nothing sensible to design if the corner has run past Nyquist.
    if (fRippleEdge_Hz >= Fs_Hz / 2.0f) fRippleEdge_Hz = Fs_Hz / 2.0f * 0.999f;

    // Ellipse shape: eps sets how much ripple, v0 how eccentric the pole locus.
    const float32_t eps = sqrtf(powf(10.0f, ripple_dB / 10.0f) - 1.0f);
    const float32_t v0 = asinhf(1.0f / eps) / (float32_t)order;
    const float32_t sinhV0 = sinhf(v0);
    const float32_t coshV0 = coshf(v0);
    const float32_t wc = PrewarpRadians(fRippleEdge_Hz, Fs_Hz);

    for (uint32_t k = 0; k < nSections; k++) {
        const float32_t theta = PI * (float32_t)(2 * k + 1) / (float32_t)(2 * order);
        const float32_t sigma = -sinhV0 * arm_sin_f32(theta) * wc;   // pole real part
        const float32_t omega = coshV0 * arm_cos_f32(theta) * wc;    // pole imaginary part
        const float32_t wn = sqrtf(sigma * sigma + omega * omega);
        const float32_t Q = wn / (2.0f * fabsf(sigma));
        // Lowpass numerator: wn^2, i.e. unity gain at DC for this section.
        BilinearBiquad(&coeffs[k * 5], 0.0f, 0.0f, wn * wn, wn, Q, Fs_Hz);
    }

    // Normalise the cascade to unity at DC. Evaluating H(z) at z = 1 reduces to
    // summing the numerator over the summed denominator for each section.
    float32_t dcGain = 1.0f;
    for (uint32_t k = 0; k < nSections; k++) {
        const float32_t *s = &coeffs[k * 5];
        dcGain *= (s[0] + s[1] + s[2]) / (1.0f - s[3] - s[4]);
    }
    if (dcGain > 0.0f) {
        // Spread the correction evenly so no single section carries a large gain.
        const float32_t perSection = powf(dcGain, -1.0f / (float32_t)nSections);
        for (uint32_t k = 0; k < nSections; k++) {
            coeffs[k * 5 + 0] *= perSection;
            coeffs[k * 5 + 1] *= perSection;
            coeffs[k * 5 + 2] *= perSection;
        }
    }
}

/**
 * Design a stagger-tuned bandpass cascade as ARM biquads.
 *
 * The prototype describes a set of second-order analog bandpass sections whose
 * resonances straddle the centre frequency, each given as a fraction of that
 * centre. Scaling those fractions to the wanted centre and running them through
 * the bilinear transform reproduces the same response shape at any centre
 * frequency and any sample rate.
 *
 * Sections are emitted with b0 negative and b2 positive, matching the sign
 * convention of the equaliser tables this replaces. ApplyEQBandFilter() sums
 * adjacent bands with alternating sign, and that reconstruction depends on it.
 *
 * @param coeffs Destination, nSections sections of 5 floats
 * @param proto Prototype sections, normalised to the centre frequency
 * @param nSections Number of sections in the cascade
 * @param fc_Hz Centre frequency the cascade should peak at
 * @param gain Per-section gain, used to normalise the cascade peak
 * @param Fs_Hz Sample rate the filter will run at
 */
void CalcBandpassCascadeCoeffs(float32_t *coeffs, const BandpassProtoSection *proto,
                               uint32_t nSections, float32_t fc_Hz, float32_t gain,
                               float32_t Fs_Hz) {
    // Keep the whole cascade below Nyquist; the outermost section sits above fc.
    if (fc_Hz >= Fs_Hz / 2.0f) fc_Hz = Fs_Hz / 2.0f * 0.999f;
    const float32_t wc = PrewarpRadians(fc_Hz, Fs_Hz);

    for (uint32_t k = 0; k < nSections; k++) {
        const float32_t wn = proto[k].wnRatio * wc;
        const float32_t Q = proto[k].Q;
        // Bandpass numerator: (wn/Q)s, negated to match the table convention.
        BilinearBiquad(&coeffs[k * 5], 0.0f, -gain * wn / Q, 0.0f, wn, Q, Fs_Hz);
    }
}

/**
 * Initialize a FIR decimation filter
 * 
 * @param *filter Pointer to the DecimationFilter struct
 * @param DF The decimation factor
 * @param sample_rate_Hz Sample rate for the digital samples
 * @param att_dB The stopband attenuation in dB
 * @param bandwidth_Hz The bandwidth
 * @param blockSize The number of samples to act upon
 */
void InitializeDecimationFilter(DecimationFilter *filter, float32_t DF, float32_t sampleRate_Hz, 
                                float32_t att_dB, float32_t bandwidth_Hz, uint32_t blockSize){
  filter->M = DF;
  filter->n_samplerate_Hz = sampleRate_Hz;
  filter->n_att_dB = att_dB;
  filter->n_desired_BW_Hz = bandwidth_Hz;

  filter->n_fpass = filter->n_desired_BW_Hz / filter->n_samplerate_Hz;
  filter->n_fstop = ((filter->n_samplerate_Hz / filter->M) - filter->n_desired_BW_Hz) / filter->n_samplerate_Hz;
  filter->n_dec_taps = (1 + (uint16_t)(filter->n_att_dB / (22.0 * (filter->n_fstop - filter->n_fpass))));

  // Free any buffers from a previous initialization so that re-initializing at
  // run time (e.g. a sample-rate change) does not leak memory. The pointers are
  // nullptr on first use, and free(nullptr) is a no-op.
  free(filter->FIR_dec_I_state);
  free(filter->FIR_dec_Q_state);
  free(filter->FIR_dec_coeffs);

  filter->FIR_dec_I_state = (float32_t*)malloc(sizeof(float32_t) * (filter->n_dec_taps + blockSize - 1));
  filter->FIR_dec_Q_state = (float32_t*)malloc(sizeof(float32_t) * (filter->n_dec_taps + blockSize - 1));
  filter->FIR_dec_coeffs = (float32_t*)malloc(sizeof(float32_t) * filter->n_dec_taps);

  CalcFIRCoeffs(filter->FIR_dec_coeffs, filter->n_dec_taps, (float32_t)(filter->n_desired_BW_Hz), 
                filter->n_att_dB, Lowpass, 0.0, (float32_t)(sampleRate_Hz));
  arm_fir_decimate_init_f32(&(filter->FIR_dec_I), filter->n_dec_taps, (uint32_t)filter->M, 
                filter->FIR_dec_coeffs, filter->FIR_dec_I_state, blockSize);
  arm_fir_decimate_init_f32(&(filter->FIR_dec_Q), filter->n_dec_taps, (uint32_t)filter->M, 
                filter->FIR_dec_coeffs, filter->FIR_dec_Q_state, blockSize);

}

/**
 * Decimate an array without filtering.
 * 
 * @param *in_buffer Pointer to the array of samples to decimate
 * @param *out_buffer Pointer to the array to put the decimated samples
 * @param M Decimation factor
 * @param blockSize The number of samples in in_buffer
 */
void decimate_f32(float32_t *in_buffer, float32_t *out_buffer, uint16_t M, uint32_t blockSize){
  for (uint32_t i = 0; i<blockSize/M; i++){
    out_buffer[i] = in_buffer[i*M];
  }
}

/**
 * Calculate the FFT of the FIR filter coefficients once to produce the FIR filter mask.
 */
void InitFilterMask(float32_t *FIR_filter_mask, ReceiveFilterConfig *RXfilters) {
    // the FIR has exactly m_NumTaps = (FFT_length / 2) + 1 coefficients, 
    // so we have to add (FFT_length / 2) -1 zeros before the FFT in order to produce a FFT_length 
    // point input buffer for the FFT
    // copy coefficients into real values of first part of buffer, rest is zero

    float32_t FIR_Coef_I[RXfilters->m_NumTaps];
    float32_t FIR_Coef_Q[RXfilters->m_NumTaps];
    int32_t high_Hz, low_Hz;
    if (ED.modulation[ED.activeVFO] == bands[ED.currentBand[ED.activeVFO]].mode){
        high_Hz = bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
        low_Hz = bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz;
    } else {
        // we have changed from the default modulation
        switch (ED.modulation[ED.activeVFO]){
            case LSB:
            case USB:
                low_Hz  = -bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
                high_Hz = -bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz;
                break;
            case SAM:
            case AM:
            case IQ:
            case DCF77:
                #define MAXABS(a, b) ((abs(a)) > (abs(b)) ? (abs(a)) : (abs(b)))
                int32_t edge_Hz = MAXABS(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz,
                                         bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz); 
                low_Hz = -edge_Hz;
                high_Hz = edge_Hz;
                break;
        }
    }
    CalcCplxFIRCoeffs(FIR_Coef_I, FIR_Coef_Q, RXfilters->m_NumTaps, 
        (float32_t)low_Hz, 
        (float32_t)high_Hz, 
        (float)SR[SampleRate].rate / RXfilters->DF);

    for (size_t i = 0; i < RXfilters->m_NumTaps; i++) {
        FIR_filter_mask[i * 2] = FIR_Coef_I[i];
        FIR_filter_mask[i * 2 + 1] = FIR_Coef_Q[i];
    }

    for (size_t i = FFT_LENGTH + 1; i < FFT_LENGTH * 2; i++) {
        FIR_filter_mask[i] = 0.0;
    }

    // Used by unit tests
    if (dspfirfilename != nullptr){
        WriteFloatFile(FIR_filter_mask, 2*FFT_LENGTH, dspfirfilename);
    }

    // FFT of FIR_filter_mask
    // perform FFT (in-place), needs only to be done once (or every time the filter coeffs change)
    FFT512Forward(FIR_filter_mask);
}

/**
 * Used by the unit tests
 */
void setdspfirfilename(char *fnm){
    dspfirfilename = fnm; // "FIR_filter_samples.txt"
}

// ============================================================================
//  Run-time filter generation
//
//  The design constants below were recovered from the coefficient tables the
//  radio used to ship, by code/tools/extract_filter_prototypes.py. They are the
//  design spec, not derived values: re-run that script against
//  code/test/reference_filters.cpp if they ever need to be checked.
// ============================================================================

/** CW audio lowpass filters share an order and a ripple; only the corner differs. */
#define CW_AUDIO_FILTER_COUNT   5
#define CW_AUDIO_FILTER_ORDER   12
#define CW_AUDIO_FILTER_RIPPLE_DB 0.02f

/** Number of biquad sections in each equaliser cell. Matches ReceiveFilterConfig::eqNumStages. */
#define EQ_PROTO_SECTIONS 4

/** Edge of the equiripple region of each CW audio filter, at the labelled
 *  cutoffs 840, 1080, 1320, 1800 and 2000 Hz. A Chebyshev is specified by its
 *  ripple edge, which sits a few percent below the nominal cutoff. */
static const float32_t CW_AUDIO_RIPPLE_EDGE_HZ[CW_AUDIO_FILTER_COUNT] = {
    807.1f, 1038.0f, 1269.0f, 1731.5f, 1963.2f
};

/** Centre frequency of each equaliser cell. */
static const float32_t EQ_BAND_FC_HZ[EQUALIZER_CELL_COUNT] = {
    198.425f, 250.000f, 314.980f, 400.000f, 500.000f,
    630.000f, 793.000f, 1000.000f, 1259.000f, 1587.000f,
    2000.000f, 2500.000f, 3150.000f, 4000.000f
};

/** Per-section gain that puts each equaliser cell's peak at unity. */
static const float32_t EQ_BAND_GAIN[EQUALIZER_CELL_COUNT] = {
    1.184185753f, 1.183486381f, 1.183688167f, 1.183936689f,
    1.183431106f, 1.184571413f, 1.183589930f, 1.183974027f,
    1.183805406f, 1.183267466f, 1.184229387f, 1.184796361f,
    1.186649928f, 1.189574151f
};

/** Analog prototype of each equaliser cell: four bandpass sections whose
 *  resonances straddle the centre frequency, given as fractions of it. */
static const BandpassProtoSection EQ_BAND_PROTO[EQUALIZER_CELL_COUNT][EQ_PROTO_SECTIONS] = {
    {{0.951083615f, 2.466689083f}, {1.051379253f, 2.466689084f}, {0.851636566f, 2.863638706f}, {1.174150596f, 2.863638705f}},
    {{0.953178819f, 2.574736321f}, {1.049087493f, 2.574736321f}, {0.857535414f, 2.986184529f}, {1.166095255f, 2.986184529f}},
    {{0.952570489f, 2.542065805f}, {1.049764384f, 2.542065806f}, {0.855807346f, 2.949116270f}, {1.168457571f, 2.949116270f}},
    {{0.951830713f, 2.503540450f}, {1.050585940f, 2.503540450f}, {0.853713229f, 2.905420150f}, {1.171330056f, 2.905420150f}},
    {{0.953370464f, 2.583905422f}, {1.048927026f, 2.583905422f}, {0.858034309f, 2.996589913f}, {1.165473263f, 2.996589913f}},
    {{0.949994428f, 2.412809599f}, {1.052637753f, 2.412809599f}, {0.848532629f, 2.802580881f}, {1.178505064f, 2.802580881f}},
    {{0.952874313f, 2.557802231f}, {1.049445688f, 2.557802231f}, {0.856652873f, 2.966969526f}, {1.167322110f, 2.966969526f}},
    {{0.951691136f, 2.497903978f}, {1.050676047f, 2.497903978f}, {0.853375360f, 2.899028582f}, {1.171722464f, 2.899028582f}},
    {{0.952233947f, 2.523659752f}, {1.050168894f, 2.523659752f}, {0.854827001f, 2.928237726f}, {1.169834914f, 2.928237726f}},
    {{0.953866388f, 2.611676440f}, {1.048364858f, 2.611676440f}, {0.859456463f, 3.028110638f}, {1.163526070f, 3.028110638f}},
    {{0.950982372f, 2.460405147f}, {1.051548607f, 2.460405147f}, {0.851302026f, 2.856515705f}, {1.174676152f, 2.856515705f}},
    {{0.949340102f, 2.383020780f}, {1.053327036f, 2.383020780f}, {0.846732326f, 2.768839341f}, {1.180970143f, 2.768839341f}},
    {{0.944376056f, 2.174506313f}, {1.058900206f, 2.174506313f}, {0.832947335f, 2.533023428f}, {1.200556094f, 2.533023428f}},
    {{0.937307103f, 1.936908781f}, {1.066883605f, 1.936908781f}, {0.813799101f, 2.265312153f}, {1.228801532f, 2.265312153f}}
};

/** -6 dB corner of the CW decoder input filter. CalcFIRCoeffs takes its fc
 *  argument as the -6 dB point, so this is not the 1560 Hz the filter is
 *  nominally described by - that is its -3 dB point. */
#define CW_DECODE_FIR_FC_HZ 1749.1f

/** -6 dB corner of the transmit interpolate-by-2 filter, which is what limits
 *  the transmit audio bandwidth (-3 dB at 2759 Hz). */
#define TX_AUDIO_LPF_FC_HZ 3039.6f

/** -6 dB corner of the transmit decimate-by-2 filter feeding the Hilbert stage.
 *
 *  This one is not a reproduction of the table it replaces. That table was flat
 *  to 0.425 * Fs, far past the 0.25 * Fs a decimate-by-2 stage needs, so
 *  everything between 6 and 9.5 kHz folded back into the transmit audio. A real
 *  lowpass here removes that aliasing. 3.5 kHz sits above the 2.76 kHz the
 *  audio bandwidth filter allows through, so it costs no wanted signal. */
#define TX_DECIMATE3_FC_HZ 3500.0f

/** Stopband attenuation asked of the generated FIR stages. */
#define GENERATED_FIR_ATTENUATION_DB 90.0f

/**
 * Scale a set of FIR taps to unity gain at DC.
 *
 * CalcFIRCoeffs does not normalise its output, and the CW decoder compares the
 * filtered magnitude against a fixed threshold, so the gain has to be pinned.
 *
 * @param taps Coefficient array, modified in place
 * @param nTaps Number of taps
 */
static void NormalizeFIRDCGain(float32_t *taps, uint32_t nTaps) {
    float32_t sum = 0.0f;
    for (uint32_t i = 0; i < nTaps; i++) sum += taps[i];
    if (sum == 0.0f) return;
    for (uint32_t i = 0; i < nTaps; i++) taps[i] /= sum;
}

/**
 * Regenerate the receive audio filter tables for the current sample rate.
 *
 * Called from the top of InitializeFilters(), so it runs at startup and again
 * on every sample rate change, before the ARM instances are bound.
 *
 * Filters that specify their corner as a fraction of the sample rate - the
 * decimation and interpolation anti-alias stages, the zoom FFT filters - are
 * deliberately not regenerated. Those are already correct at any rate, because
 * scaling with Fs is exactly what an anti-alias filter should do.
 *
 * @param audioFs_Hz Sample rate of the decimated audio stream
 */
void InitializeReceiveAudioFilterCoeffs(float32_t audioFs_Hz) {
    // CW audio lowpass filters, 12 pole Chebyshev type I.
    float32_t *cwAudio[CW_AUDIO_FILTER_COUNT] = {
        CW_AudioFilterCoeffs1, CW_AudioFilterCoeffs2, CW_AudioFilterCoeffs3,
        CW_AudioFilterCoeffs4, CW_AudioFilterCoeffs5
    };
    for (uint32_t i = 0; i < CW_AUDIO_FILTER_COUNT; i++) {
        CalcChebyshevILowpassCoeffs(cwAudio[i], CW_AUDIO_FILTER_ORDER,
                                    CW_AUDIO_FILTER_RIPPLE_DB,
                                    CW_AUDIO_RIPPLE_EDGE_HZ[i], audioFs_Hz);
    }

    // Audio equaliser cells. The receive and transmit chains share these tables
    // and both run at the audio rate, so one set serves both.
    for (uint32_t i = 0; i < EQUALIZER_CELL_COUNT; i++) {
        CalcBandpassCascadeCoeffs(*EQ_Coeffs[i], EQ_BAND_PROTO[i], EQ_PROTO_SECTIONS,
                                  EQ_BAND_FC_HZ[i], EQ_BAND_GAIN[i], audioFs_Hz);
    }

    // CW decoder input filter.
    CalcFIRCoeffs(CW_Filter_Coeffs2, 64, CW_DECODE_FIR_FC_HZ, GENERATED_FIR_ATTENUATION_DB,
                  Lowpass, 0.0f, audioFs_Hz);
    NormalizeFIRDCGain(CW_Filter_Coeffs2, 64);
}

/**
 * Regenerate the transmit filter tables for the current sample rate.
 *
 * Called from the top of InitializeTransmitFilters(). Only the two stages
 * either side of the Hilbert transform are generated: the rest of the transmit
 * chain is anti-alias and anti-image filtering specified as a fraction of Fs,
 * which needs no adjustment.
 *
 * A decimating FIR filters at its input rate and an interpolating one at its
 * output rate. For both of these stages that rate is the audio rate, not the
 * 12 ksps the Hilbert transform between them runs at.
 *
 * @param audioFs_Hz Sample rate of the decimated audio stream
 */
void InitializeTransmitFilterCoeffs(float32_t audioFs_Hz) {
    CalcFIRCoeffs(coeffs12K_8K_LPF_FIR, 48, TX_DECIMATE3_FC_HZ, GENERATED_FIR_ATTENUATION_DB,
                  Lowpass, 0.0f, audioFs_Hz);
    NormalizeFIRDCGain(coeffs12K_8K_LPF_FIR, 48);

    CalcFIRCoeffs(FIR_int3_12ksps_48tap_2k7, 48, TX_AUDIO_LPF_FC_HZ, GENERATED_FIR_ATTENUATION_DB,
                  Lowpass, 0.0f, audioFs_Hz);
    NormalizeFIRDCGain(FIR_int3_12ksps_48tap_2k7, 48);
}
