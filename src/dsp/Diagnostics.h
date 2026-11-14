#pragma once
#include <atomic>

namespace wsho::dsp
{
// Number of times a NaN / Inf safety net had to fire (voice filter reset or
// engine-level block mute). It should stay at zero: the nets exist so a bug
// can never reach the speakers, and the self-test asserts they are not what
// keeps the output clean. Dev diagnostics only — nothing in the plugin reads it.
std::atomic<int>& guardTrips() noexcept;
} // namespace wsho::dsp
