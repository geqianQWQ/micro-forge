#pragma once

#include <iosfwd>

namespace micro_forge::chips::stm32f1 {
class Stm32f103Soc;
}

namespace micro_forge::cli {

// Write a JSON snapshot of the SoC state to `out`.
// B2 scope: cpu / fault / run regions.
// peripherals / events regions are stubbed (filled in B3).
void write_snapshot_json(chips::stm32f1::Stm32f103Soc& soc,
                         std::ostream& out);

} // namespace micro_forge::cli
