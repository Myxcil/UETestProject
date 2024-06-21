#pragma once

#include "FireSimulationConfig.generated.h"

USTRUCT()
struct FIRESIMULATION_API FFireSimulationConfig
{
	GENERATED_BODY()
	
	float CellSize = 10.0f;
	int32 MaxResolution = 128;
	int32 FluidResolutionScale = 2;
	int32 NumPressureIterations = 8;

	FVector4f FluidDissipation = FVector4f(0.001f, 0.0f, 0.03f, 0.03f);
	FVector4f FluidDecay = FVector4f(0.0f, 0.2f, 0.0f, 0.0f);
};
