#pragma once

namespace phoenix {

// LiPo state-of-charge estimate from open-circuit voltage. A LiPo's
// discharge curve is flat around 3.7-3.9 V and falls off a cliff below
// ~3.5 V, so a naive linear map between 3.0 and 4.2 V would sit at "40%"
// for most of the pack's life and then die at "25%". This uses a piecewise-
// linear fit of a typical 1S discharge curve instead. Clamped to [0, 100].
int lipoPercentFromMillivolts(int millivolts);

}  // namespace phoenix
