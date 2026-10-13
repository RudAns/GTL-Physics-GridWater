#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GWGameMode.generated.h"

class AGWWorld;

UCLASS()
class GRIDWATER_API AGWGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AGWGameMode();
	virtual void StartPlay() override;
	UPROPERTY() TObjectPtr<AGWWorld> WaterWorld;
};
