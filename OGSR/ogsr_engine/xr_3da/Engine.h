// Engine.h: interface for the CEngine class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#define DLL_API
#define ENGINE_API
#define ECORE_API

// TODO: this should be in render configuration
#define R__NUM_SUN_CASCADES (3u) // csm/s.ligts
#define R__NUM_AUX_CONTEXTS (6u) // rain/s.lights
#define R__NUM_PARALLEL_CONTEXTS (R__NUM_SUN_CASCADES + R__NUM_AUX_CONTEXTS)
#define R__NUM_CONTEXTS (R__NUM_PARALLEL_CONTEXTS + 1 /* imm */)

#include "engineAPI.h"
#include "eventAPI.h"
#include "xrSheduler.h"

class ENGINE_API CEngine
{
public:
    // DLL api stuff
    CEngineAPI External;
    CEventAPI Event;
    CSheduler Sheduler;

    void Initialize();
    void Destroy();

    CEngine() = default;
    ~CEngine() = default;
};

ENGINE_API extern CEngine Engine;
