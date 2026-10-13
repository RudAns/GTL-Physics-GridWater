#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GWCamera.generated.h"

class USpringArmComponent;
class UCameraComponent;

/** City-builder camera: pan on the ground plane, 45-degree yaw steps with smoothing, zoom that also flattens the pitch. */
UCLASS()
class GRIDWATER_API AGWCamera : public APawn
{
	GENERATED_BODY()
public:
	AGWCamera();
	virtual void Tick(float DeltaSeconds) override;

	void Pan(const FVector2D& Input, float DeltaSeconds);
	void PanPixels(const FVector2D& Delta);
	void RotateStep(int32 Dir) { YawTarget += Dir * YawStep; }
	void Zoom(float Steps) { ArmTarget = FMath::Clamp(ArmTarget * FMath::Pow(0.88f, Steps), 1200.f, 16000.f); }
	void Focus(const FVector& P, float InArm, float InYaw) { FocusTarget = P; ArmTarget = InArm; YawTarget = InYaw; }
	void SnapNow();
	float GetYaw() const { return Yaw; }
	FVector GetFocus() const { return FocusTarget; }
	float YawStep = 45.f;
	FBox2D Bounds = FBox2D(FVector2D(0, 0), FVector2D(9600, 7200));

	UPROPERTY() TObjectPtr<USpringArmComponent> Arm;
	UPROPERTY() TObjectPtr<UCameraComponent> Cam;

private:
	FVector FocusTarget = FVector(4800, 3600, 450);
	float Yaw = -90.f, YawTarget = -90.f;
	float ArmLen = 12000.f, ArmTarget = 12000.f;
};
