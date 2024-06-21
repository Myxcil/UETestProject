// Fill out your copyright notice in the Description page of Project Settings.


#include "FireSimulatorVolume.h"

#include "FireSimulation.h"


// Sets default values for this component's properties
UFireSimulatorVolume::UFireSimulatorVolume()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UFireSimulatorVolume::BeginPlay()
{
	Super::BeginPlay();

	FFireSimulationModule::Get().Initialize(VolumeSize, Config);
}

void UFireSimulatorVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}


// Called every frame
void UFireSimulatorVolume::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	FFireSimulationModule::Get().Dispatch(DeltaTime, Config);
}

