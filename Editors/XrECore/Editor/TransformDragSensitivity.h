#pragma once

#if defined(XRECORE_EXPORTS) || defined(XR_SDK_RUNTIME_BUILD)
#define TRANSFORM_SENS_API __declspec(dllexport)
#else
#define TRANSFORM_SENS_API __declspec(dllimport)
#endif

enum ETransformDragComponent
{
    tdcPosition,
    tdcRotation,
    tdcScale,
};

TRANSFORM_SENS_API float GetTransformDragSensitivityScale(ETransformDragComponent component);
