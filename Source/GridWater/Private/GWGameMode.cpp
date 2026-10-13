#include "GWGameMode.h"

#include "GWCamera.h"
#include "GWPlayerController.h"
#include "GWWorld.h"

AGWGameMode::AGWGameMode()
{
	DefaultPawnClass = AGWCamera::StaticClass();
	PlayerControllerClass = AGWPlayerController::StaticClass();
	HUDClass = nullptr;
}

void AGWGameMode::StartPlay()
{
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	WaterWorld = GetWorld()->SpawnActor<AGWWorld>(AGWWorld::StaticClass(), FTransform::Identity, P);
	Super::StartPlay();
}
