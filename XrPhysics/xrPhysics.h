#pragma once

#if defined(XRPHYSICS_EXPORTS) || defined(XR_SDK_RUNTIME_BUILD)
#define XRPHYSICS_API __declspec(dllexport)
#else
#define XRPHYSICS_API __declspec(dllimport)
#endif
