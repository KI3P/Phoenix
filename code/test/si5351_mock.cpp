/*
 * Mock version of the si5351.cpp - Si5351 library for Arduino
 *
 * Derived from original si5351.cpp which is:
 * Copyright (C) 2015 - 2016 Jason Milldrum <milldrum@gmail.com>
 *                           Dana H. Myers <k6jq@comcast.net>
 * Additions / mods by Oliver King KI3P <ki3p@cotan.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>

#include "Arduino.h"
#include "Wire.h"
#include "RFBoard_si5351.h"


/********************/
/* Public functions */
/********************/

Si5351::Si5351(uint8_t i2c_addr):
	i2c_bus_addr(i2c_addr)
{
	xtal_freq[0] = SI5351_XTAL_FREQ;

	// Start by using XO ref osc as default for each PLL
	plla_ref_osc = SI5351_PLL_INPUT_XO;
	pllb_ref_osc = SI5351_PLL_INPUT_XO;
	clkin_div = SI5351_CLKIN_DIV_1;
}

/*
 * init(uint8_t xtal_load_c, uint32_t ref_osc_freq, int32_t corr)
 *
 * Setup communications to the Si5351 and set the crystal
 * load capacitance.
 *
 * xtal_load_c - Crystal load capacitance. Use the SI5351_CRYSTAL_LOAD_*PF
 * defines in the header file
 * xo_freq - Crystal/reference oscillator frequency in 1 Hz increments.
 * Defaults to 25000000 if a 0 is used here.
 * corr - Frequency correction constant in parts-per-billion
 *
 * Returns a boolean that indicates whether a device was found on the desired
 * I2C address.
 *
 */
bool Si5351::init(uint8_t xtal_load_c, uint32_t xo_freq, int32_t corr){
    return true;
}

/*
 * reset(void)
 *
 * Call to reset the Si5351 to the state initialized by the library.
 *
 */
void Si5351::reset(void){
}

/*
 * set_freq(uint64_t freq, enum si5351_clock clk)
 *
 * Sets the clock frequency of the specified CLK output.
 * Frequency range of 8 kHz to 150 MHz
 *
 * freq - Output frequency in Hz
 * clk - Clock output
 *   (use the si5351_clock enum)
 */
uint8_t Si5351::set_freq(uint64_t freq, enum si5351_clock clk){
    clk_freq[clk] = freq;
    return 0;
}

/*
 * set_freq_manual(uint64_t freq, uint64_t pll_freq, enum si5351_clock clk)
 *
 * Sets the clock frequency of the specified CLK output using the given PLL
 * frequency. You must ensure that the MS is assigned to the correct PLL and
 * that the PLL is set to the correct frequency before using this method.
 *
 * It is important to note that if you use this method, you will have to
 * track that all settings are sane yourself.
 *
 * freq - Output frequency in Hz
 * pll_freq - Frequency of the PLL driving the Multisynth in Hz * 100
 * clk - Clock output
 *   (use the si5351_clock enum)
 */
uint8_t Si5351::set_freq_manual(uint64_t freq, uint64_t pll_freq, enum si5351_clock clk){
    clk_freq[clk] = freq;
    if (pll_assignment[clk] == SI5351_PLLA) {
        plla_freq = pll_freq;
    } else {
        pllb_freq = pll_freq;
    }
    return 0;
}

/*
 * set_pll(uint64_t pll_freq, enum si5351_pll target_pll)
 *
 * Set the specified PLL to a specific oscillation frequency
 *
 * pll_freq - Desired PLL frequency in Hz * 100
 * target_pll - Which PLL to set
 *     (use the si5351_pll enum)
 */
void Si5351::set_pll(uint64_t pll_freq, enum si5351_pll target_pll){
    if (target_pll == SI5351_PLLA) {
        plla_freq = pll_freq;
    } else {
        pllb_freq = pll_freq;
    }
}

/*
 * set_ms(enum si5351_clock clk, struct Si5351RegSet ms_reg, uint8_t int_mode, uint8_t r_div, uint8_t div_by_4)
 *
 * Set the specified multisynth parameters. Not normally needed, but public for advanced users.
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * int_mode - Set integer mode
 *  Set to 1 to enable, 0 to disable
 * r_div - Desired r_div ratio
 * div_by_4 - Set Divide By 4 mode
 *   Set to 1 to enable, 0 to disable
 */
void Si5351::set_ms(enum si5351_clock clk, struct Si5351RegSet ms_reg, uint8_t int_mode, uint8_t r_div, uint8_t div_by_4)
{}

/*
 * output_enable(enum si5351_clock clk, uint8_t enable)
 *
 * Enable or disable a chosen output
 * clk - Clock output
 *   (use the si5351_clock enum)
 * enable - Set to 1 to enable, 0 to disable
 */
void Si5351::output_enable(enum si5351_clock clk, uint8_t enable)
{
    output_enable_calls[clk] = enable;
}

/*
 * drive_strength(enum si5351_clock clk, enum si5351_drive drive)
 *
 * Sets the drive strength of the specified clock output
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * drive - Desired drive level
 *   (use the si5351_drive enum)
 */
void Si5351::drive_strength(enum si5351_clock clk, enum si5351_drive drive)
{
    drive_strength_calls[clk]++;
    drive_strength_values[clk] = drive;
}

/*
 * update_status(void)
 *
 * Call this to update the status structs, then access them
 * via the dev_status and dev_int_status global members.
 *
 * See the header file for the struct definitions. These
 * correspond to the flag names for registers 0 and 1 in
 * the Si5351 datasheet.
 */
void Si5351::update_status(void)
{}

/*
 * set_correction(int32_t corr, enum si5351_pll_input ref_osc)
 *
 * corr - Correction factor in ppb
 * ref_osc - Desired reference oscillator
 *     (use the si5351_pll_input enum)
 *
 * Use this to set the oscillator correction factor.
 * This value is a signed 32-bit integer of the
 * parts-per-billion value that the actual oscillation
 * frequency deviates from the specified frequency.
 *
 * The frequency calibration is done as a one-time procedure.
 * Any desired test frequency within the normal range of the
 * Si5351 should be set, then the actual output frequency
 * should be measured as accurately as possible. The
 * difference between the measured and specified frequencies
 * should be calculated in Hertz, then multiplied by 10 in
 * order to get the parts-per-billion value.
 *
 * Since the Si5351 itself has an intrinsic 0 PPM error, this
 * correction factor is good across the entire tuning range of
 * the Si5351. Once this calibration is done accurately, it
 * should not have to be done again for the same Si5351 and
 * crystal.
 */
void Si5351::set_correction(int32_t corr, enum si5351_pll_input ref_osc)
{}

/*
 * set_phase(enum si5351_clock clk, uint8_t phase)
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * phase - 7-bit phase word
 *   (in units of VCO/4 period)
 *
 * Write the 7-bit phase register. This must be used
 * with a user-set PLL frequency so that the user can
 * calculate the proper tuning word based on the PLL period.
 */
void Si5351::set_phase(enum si5351_clock clk, uint8_t phase)
{
    phase_calls[clk]++;
    phase_values[clk] = phase;
}

/*
 * get_correction(enum si5351_pll_input ref_osc)
 *
 * ref_osc - Desired reference oscillator
 *     0: crystal oscillator (XO)
 *     1: external clock input (CLKIN)
 *
 * Returns the oscillator correction factor stored
 * in RAM.
 */
int32_t Si5351::get_correction(enum si5351_pll_input ref_osc)
{
	return 0;
}

/*
 * pll_reset(enum si5351_pll target_pll)
 *
 * target_pll - Which PLL to reset
 *     (use the si5351_pll enum)
 *
 * Apply a reset to the indicated PLL.
 */
void Si5351::pll_reset(enum si5351_pll target_pll)
{
    pll_reset_calls[target_pll]++;
}

/*
 * set_ms_source(enum si5351_clock clk, enum si5351_pll pll)
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * pll - Which PLL to use as the source
 *     (use the si5351_pll enum)
 *
 * Set the desired PLL source for a multisynth.
 */
void Si5351::set_ms_source(enum si5351_clock clk, enum si5351_pll pll)
{
    pll_assignment[clk] = pll;
}

/*
 * set_int(enum si5351_clock clk, uint8_t int_mode)
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * enable - Set to 1 to enable, 0 to disable
 *
 * Set the indicated multisynth into integer mode.
 */
void Si5351::set_int(enum si5351_clock clk, uint8_t enable)
{}

/*
 * set_clock_pwr(enum si5351_clock clk, uint8_t pwr)
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * pwr - Set to 1 to enable, 0 to disable
 *
 * Enable or disable power to a clock output (a power
 * saving feature).
 */
void Si5351::set_clock_pwr(enum si5351_clock clk, uint8_t pwr)
{}

/*
 * set_clock_invert(enum si5351_clock clk, uint8_t inv)
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * inv - Set to 1 to enable, 0 to disable
 *
 * Enable to invert the clock output waveform.
 */
void Si5351::set_clock_invert(enum si5351_clock clk, uint8_t inv)
{}

/*
 * set_clock_source(enum si5351_clock clk, enum si5351_clock_source src)
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * src - Which clock source to use for the multisynth
 *   (use the si5351_clock_source enum)
 *
 * Set the clock source for a multisynth (based on the options
 * presented for Registers 16-23 in the Silicon Labs AN619 document).
 * Choices are XTAL, CLKIN, MS0, or the multisynth associated with
 * the clock output.
 */
void Si5351::set_clock_source(enum si5351_clock clk, enum si5351_clock_source src)
{}

/*
 * set_clock_disable(enum si5351_clock clk, enum si5351_clock_disable dis_state)
 *
 * clk - Clock output
 *   (use the si5351_clock enum)
 * dis_state - Desired state of the output upon disable
 *   (use the si5351_clock_disable enum)
 *
 * Set the state of the clock output when it is disabled. Per page 27
 * of AN619 (Registers 24 and 25), there are four possible values: low,
 * high, high impedance, and never disabled.
 */
void Si5351::set_clock_disable(enum si5351_clock clk, enum si5351_clock_disable dis_state)
{}

/*
 * set_clock_fanout(enum si5351_clock_fanout fanout, uint8_t enable)
 *
 * fanout - Desired clock fanout
 *   (use the si5351_clock_fanout enum)
 * enable - Set to 1 to enable, 0 to disable
 *
 * Use this function to enable or disable the clock fanout options
 * for individual clock outputs. If you intend to output the XO or
 * CLKIN on the clock outputs, enable this first.
 *
 * By default, only the Multisynth fanout is enabled at startup.
 */
void Si5351::set_clock_fanout(enum si5351_clock_fanout fanout, uint8_t enable)
{}

/*
 * set_pll_input(enum si5351_pll pll, enum si5351_pll_input input)
 *
 * pll - Which PLL to use as the source
 *     (use the si5351_pll enum)
 * input - Which reference oscillator to use as PLL input
 *     (use the si5351_pll_input enum)
 *
 * Set the desired reference oscillator source for the given PLL.
 */
void Si5351::set_pll_input(enum si5351_pll pll, enum si5351_pll_input input)
{}

/*
 * set_vcxo(uint64_t pll_freq, uint8_t ppm)
 *
 * pll_freq - Desired PLL base frequency in Hz * 100
 * ppm - VCXO pull limit in ppm
 *
 * Set the parameters for the VCXO on the Si5351B.
 */
void Si5351::set_vcxo(uint64_t pll_freq, uint8_t ppm)
{}

/*
 * set_ref_freq(uint32_t ref_freq, enum si5351_pll_input ref_osc)
 *
 * ref_freq - Reference oscillator frequency in Hz
 * ref_osc - Which reference oscillator frequency to set
 *    (use the si5351_pll_input enum)
 *
 * Set the reference frequency value for the desired reference oscillator
 */
void Si5351::set_ref_freq(uint32_t ref_freq, enum si5351_pll_input ref_osc)
{}

uint8_t Si5351::si5351_write_bulk(uint8_t addr, uint8_t bytes, uint8_t *data)
{
	return 0;
}

uint8_t Si5351::si5351_write(uint8_t addr, uint8_t data)
{
	return 0;
}

uint8_t Si5351::si5351_read(uint8_t addr)
{
	return 0;
}

/*********************/
/* Private functions */
/*********************/

uint64_t Si5351::pll_calc(enum si5351_pll pll, uint64_t freq, struct Si5351RegSet *reg, int32_t correction, uint8_t vcxo)
{
	return 0;
}

uint64_t Si5351::multisynth_calc(uint64_t freq, uint64_t pll_freq, struct Si5351RegSet *reg)
{
	return 0;
}

uint64_t Si5351::multisynth67_calc(uint64_t freq, uint64_t pll_freq, struct Si5351RegSet *reg)
{
    return 0;
}

void Si5351::update_sys_status(struct Si5351Status *status)
{}

void Si5351::update_int_status(struct Si5351IntStatus *int_status)
{}

void Si5351::ms_div(enum si5351_clock clk, uint8_t r_div, uint8_t div_by_4)
{}

uint8_t Si5351::select_r_div(uint64_t *freq)
{
	return 0;
}

uint8_t Si5351::select_r_div_ms67(uint64_t *freq)
{
	return 0;
}
