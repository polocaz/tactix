// Fill out your copyright notice in the Description page of Project Settings.


#include "TactixPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Player/TactixCameraManager.h"
#include "InputMappingContext.h"


ATactixPlayerController::ATactixPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetGenericTeamId(FGenericTeamId(2));
	PlayerCameraManagerClass = ATactixCameraManager::StaticClass();

}

void ATactixPlayerController::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	if (TeamId != NewTeamId)
	{
		TeamId = NewTeamId;
	}
}

void ATactixPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Add Input Mapping Context
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
		{
			Subsystem->AddMappingContext(CurrentContext, 0);
		}
	}
}
