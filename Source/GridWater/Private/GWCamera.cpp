#include "GWCamera.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

AGWCamera::AGWCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(RootComponent);
	Arm->bDoCollisionTest = false;
	Arm->bEnableCameraLag = false;
	Cam = CreateDefaultSubobject<UCameraComponent>(TEXT("Cam"));
	Cam->SetupAttachment(Arm);
	Cam->SetFieldOfView(38.f);
}

void AGWCamera::Pan(const FVector2D& Input, float DeltaSeconds)
{
	if (Input.IsNearlyZero()) return;
	const FRotator R(0.f, Yaw, 0.f);
	const FVector F = R.Vector(), Rt = FRotationMatrix(R).GetUnitAxis(EAxis::Y);
	FocusTarget += (F * Input.Y + Rt * Input.X) * ArmLen * 0.9f * DeltaSeconds;
}

void AGWCamera::PanPixels(const FVector2D& Delta)
{
	const FRotator R(0.f, Yaw, 0.f);
	const FVector F = R.Vector(), Rt = FRotationMatrix(R).GetUnitAxis(EAxis::Y);
	const float K = ArmLen * 0.0016f;
	FocusTarget += (-Rt * Delta.X + F * Delta.Y) * K;
}

void AGWCamera::SnapNow()
{
	Yaw = YawTarget; ArmLen = ArmTarget;
	SetActorLocation(FocusTarget);
	Tick(0.f);
}

void AGWCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FocusTarget.X = FMath::Clamp(FocusTarget.X, Bounds.Min.X, Bounds.Max.X);
	FocusTarget.Y = FMath::Clamp(FocusTarget.Y, Bounds.Min.Y, Bounds.Max.Y);
	const float A = DeltaSeconds <= 0.f ? 1.f : 1.f - FMath::Exp(-DeltaSeconds * 9.f);
	Yaw = FMath::Lerp(Yaw, YawTarget, A);
	ArmLen = FMath::Lerp(ArmLen, ArmTarget, A);
	SetActorLocation(FMath::Lerp(GetActorLocation(), FocusTarget, A));
	// closer = flatter view, as in most city builders
	const float T = FMath::Clamp((ArmLen - 1200.f) / (16000.f - 1200.f), 0.f, 1.f);
	Arm->TargetArmLength = ArmLen;
	Arm->SetRelativeRotation(FRotator(FMath::Lerp(-38.f, -58.f, T), Yaw, 0.f));
}
