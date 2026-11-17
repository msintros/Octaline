#pragma once

#include "../dsp/Engine.h"

#include <vector>

namespace octaline
{

struct FactoryPreset
{
    const char* name;
    EngineParams params;
};

// The factory patches from the UD-Stomp patch list (loop sampling patches excluded).
const std::vector<FactoryPreset>& factoryPresets();

} // namespace octaline
