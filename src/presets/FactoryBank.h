#pragma once
#include "../PresetManager.h"

namespace wsho
{
// The read-only "Factory" bank, compiled into the binary (FactoryBank.cpp).
// Presets list only the parameters that differ from the defaults.
Bank makeFactoryBank();
} // namespace wsho
