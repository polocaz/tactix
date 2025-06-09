// Copyright Epic Games, Inc. All Rights Reserved.

#include "TactixGameMode.h"
#include "TactixCharacter.h"
#include "UObject/ConstructorHelpers.h"

ATactixGameMode::ATactixGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
