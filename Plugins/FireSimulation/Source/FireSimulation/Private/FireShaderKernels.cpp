// Fill out your copyright notice in the Description page of Project Settings.


#include "FireShaderKernels.h"

IMPLEMENT_GLOBAL_SHADER(FFireShaderClearFloatCS, "/FireSimulation/Private/FireSimulation.usf", "CSClearFloat", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderClearFloat4CS, "/FireSimulation/Private/FireSimulation.usf", "CSClearFloat4", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderPrepareFluidDataAdvectionCS, "/FireSimulation/Private/FireSimulation.usf", "CSPrepareFluidDataAdvection", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderAdvectFluidDataCS, "/FireSimulation/Private/FireSimulation.usf", "CSAdvectFluidData", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderAdvectVelocityCS, "/FireSimulation/Private/FireSimulation.usf", "CSAdvectVelocity", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderBuoyancyCS, "/FireSimulation/Private/FireSimulation.usf", "CSBuoyancy", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderExtinguishCS, "/FireSimulation/Private/FireSimulation.usf", "CSExtinguish", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderVorticityCS, "/FireSimulation/Private/FireSimulation.usf", "CSVorticity", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderConfinementCS, "/FireSimulation/Private/FireSimulation.usf", "CSConfinement", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderDivergenceCS, "/FireSimulation/Private/FireSimulation.usf", "CSDivergence", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderPreparePressureCS, "/FireSimulation/Private/FireSimulation.usf", "CSPreparePressure", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderPressureCS, "/FireSimulation/Private/FireSimulation.usf", "CSPressure", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FFireShaderProjectionCS, "/FireSimulation/Private/FireSimulation.usf", "CSProjection", SF_Compute);
