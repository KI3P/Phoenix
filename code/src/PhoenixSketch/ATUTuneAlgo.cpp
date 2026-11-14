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
 * @file ATUTuneAlgo.cpp
 * @brief The automatic tuning program (ATU_PROG_TUNE). Not written yet: it fails at once.
 */

#include "SDT.h"

#ifdef ATU_PRESENT

#include "ATUTune.h"

static void TuneBegin(ATUTuneRecord *rec, uint32_t operatorSetting, double f_Hz){}

static ATUStep TuneNext(ATUTuneRecord *rec){
    return {ATU_STEP_FAIL, 0, ATU_FAIL_NO_MATCH};
}

static void TuneReport(ATUTuneRecord *rec, uint32_t setting, float gamma){}

const ATUPlanner ATUTunePlanner = {TuneBegin, TuneNext, TuneReport};

#endif // ATU_PRESENT
