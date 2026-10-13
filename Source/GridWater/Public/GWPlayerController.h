#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GWWorld.h"
#include "GWPlayerController.generated.h"

class AGWCamera;
class UGWHud;
class AStaticMeshActor;

UCLASS()
class GRIDWATER_API AGWPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AGWPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	AGWWorld* GetWater() const { return Water; }
	AGWCamera* GetCam() const;

	// ---- tools ----
	EGWTool Tool = EGWTool::None;
	int32 Dir = 0;
	bool bDirManual = false;
	int32 HoverCell = INDEX_NONE;
	bool bHoverValid = false;
	FString HoverWhy;
	void SelectTool(EGWTool T);
	static FString ToolName(EGWTool T);
	static FString ToolKey(EGWTool T);
	static FString ToolHelp(EGWTool T);

	// ---- principle switches (report ch.03-08) ----
	static constexpr int32 NumPrinciples = 7;
	FString PrincipleName(int32 I) const;
	FString PrincipleValue(int32 I) const;
	bool PrincipleIsOriginal(int32 I) const;
	void TogglePrinciple(int32 I);

	// ---- parameters ----
	static constexpr int32 NumParams = 5;
	FString ParamName(int32 I) const;
	FString ParamValue(int32 I) const;
	void AdjustParam(int32 I, int32 Delta);

	void SetOverlay(int32 I);
	static FString OverlayName(int32 I);
	void SetSpeed(int32 I);
	void NextPhase();
	void ToggleHazard();
	void StartGame();
	void Toast(const FString& Title, const FString& Body);

	bool bIntro = true;
	bool bUiVisible = true;
	bool bInspector = true;
	bool bLabOpen = true;
	FString ToastTitle, ToastBody;
	float ToastTime = -100.f;

private:
	UPROPERTY() TObjectPtr<AGWWorld> Water;
	UPROPERTY() TObjectPtr<UGWHud> Hud;
	UPROPERTY() TObjectPtr<AStaticMeshActor> Highlight;
	TSet<int32> DragCells;
	bool bDragging = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	int32 SpeedBeforePause = 1;

	void HandleKeys(float DeltaTime);
	void UpdateHover();
	void ApplyAt(int32 CellIdx, bool bFirst);
	float WheelDelta();

	// scripted screenshots: -GWCapture=<dir>
	FString CaptureDir;
	int32 CaptureStep = -1;
	float CaptureTimer = 0.f;
	int32 CaptureFrames = 0;
	bool bShotPending = false;
	void TickCapture(float DeltaTime);
public:
	// input injection used by the self-test (same PlayerInput path as real keys and clicks)
	void Press(const FKey& Key, int32 HoldFrames = 2);
	bool MouseToCell(int32 CellIdx);
	void Check(const TCHAR* Name, bool bOk, const FString& Detail);
	TArray<TPair<FKey, int32>> PendingReleases;
	int32 TestPass = 0, TestFail = 0;
	TMap<FString, float> Memo;
};
