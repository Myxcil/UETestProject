#pragma once

#include "FireSimulationConfig.generated.h"

USTRUCT()
struct FIRESIMULATION_API FFireSimulationConfig
{
	GENERATED_BODY()
	
	int32 NumPressureIterations = 8;
	FIntVector3 Resolution = { 128, 128,128 };
	int32 FluidResolutionScale = 2; 
};
