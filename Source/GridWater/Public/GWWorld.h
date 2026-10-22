#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterSim.h"
#include "GWWorld.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;

UENUM()
enum class EGWTool : uint8 { None, Levee, Dam, Floodgate, Valve, Pump, Dig, Raise, Erase };

UENUM()
enum class EGWPhase : uint8 { Temperate, Drought, Badtide };

/** Overlay views (report ch.11.5 debug views). */
UENUM()
enum class EGWOverlay : uint8 { Natural, Depth, Speed, Contamination, Saturation, Moisture, SoilContamination, Count };

USTRUCT()
struct FGWStructure
{
	GENERATED_BODY()
	UPROPERTY() EGWTool Type = EGWTool::None;
	UPROPERTY() int32 Cell = 0;
	UPROPERTY() int32 Dir = 0;
	UPROPERTY() float GateH = 1.5f;		// floodgate height in blocks
	UPROPERTY() int32 LeveeH = 2;		// levee height in blocks (1..3)
	UPROPERTY() float LeafW = 0.9f;		// gate leaf / shutter width and offset across the channel (joined runs fill the gaps)
	UPROPERTY() float LeafOff = 0.f;
	UPROPERTY() bool bAuto = true;		// valve closes itself on contamination
	UPROPERTY() bool bClosed = false;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Moving = nullptr;	// gate leaf / shutter / pump wheel
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Lamp = nullptr;
	float Anim = 0.f;
	double Pumped = 0, PumpedBad = 0;
};

struct FGWPlant
{
	int32 Cell = 0;
	int32 Kind = 0;			// 0 tree, 1 crop
	int32 Size = 0;			// mesh variant
	float Health = 1.f;
	bool bDead = false;
	int32 Inst = INDEX_NONE;
	FTransform Xf;
};

UCLASS()
class GRIDWATER_API AGWWorld : public AActor
{
	GENERATED_BODY()
public:
	AGWWorld();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- grid ----
	static constexpr int32 GW = 96;
	static constexpr int32 GH = 72;
	static constexpr float Cell = 100.f;		// cm per tile
	static constexpr float Block = 100.f;		// cm per terrain step
	FWaterSim Sim;

	// ---- time & seasons (demo compression: 1 day = 60 simulated seconds) ----
	static constexpr float DayLen = 60.f;
	static constexpr float SubDt = 0.3f;		// 0.6 s tick / 2 substeps, as in the game
	static constexpr float TemperateDays = 4.f;
	static constexpr float HazardDays = 2.f;
	float SimTime = 0.f;
	int32 SpeedIndex = 1;						// 0 pause, 1 x1, 2 x2, 3 x4
	float SpeedFactor() const { static const float F[] = { 0.f, 1.f, 2.f, 4.f }; return F[FMath::Clamp(SpeedIndex, 0, 3)]; }
	int32 Cycle = 1;
	EGWPhase Phase = EGWPhase::Temperate;
	float PhaseStart = 0.f;
	EGWPhase NextHazard = EGWPhase::Badtide;
	float SourceStrength = 3.0f;				// cells^3 / s total
	float SourceModifier = 1.f;
	float SourceContamination = 0.f;
	float PhaseDaysLeft() const;
	float PhaseLengthDays() const { return Phase == EGWPhase::Temperate ? TemperateDays : HazardDays; }
	float DayOfPhase() const { return (SimTime - PhaseStart) / DayLen; }
	void AdvancePhase();
	void ToggleNextHazard() { NextHazard = NextHazard == EGWPhase::Drought ? EGWPhase::Badtide : EGWPhase::Drought; }

	// ---- view ----
	EGWOverlay Overlay = EGWOverlay::Natural;
	bool bGridLines = false;
	bool bFlowArrows = false;
	void SetOverlay(EGWOverlay O);

	// ---- structures & terraforming ----
	UPROPERTY() TArray<FGWStructure> Structures;
	int32 FindStructure(int32 CellIdx) const;
	bool CanPlace(EGWTool Tool, int32 CellIdx, FString* Why = nullptr) const;
	bool Place(EGWTool Tool, int32 CellIdx, int32 Dir, float GateH = 1.5f);
	bool RemoveAt(int32 CellIdx);
	void AdjustGate(int32 CellIdx, float Delta);
	void ToggleValve(int32 CellIdx);
	/** Cells of the structure run connected to CellIdx: same type, 4-neighbours, and for gates/valves/dams the same flow axis. */
	TArray<int32> ConnectedGroup(int32 CellIdx) const;
	/** Synchronised height change for a selected run, as in the game: every member takes the clicked one's new height. */
	void AdjustGroupHeight(const TArray<int32>& Cells, int32 Primary, int32 Steps);
	/** 0 automatic, 1 forced open, 2 forced closed */
	void SetValveMode(const TArray<int32>& Cells, int32 Mode);
	TArray<int32> Selected;
	int32 SelectedPrimary = INDEX_NONE;
	void SetSelection(int32 CellIdx);
	void RefreshSelectionVisual();
	bool Terraform(int32 CellIdx, int32 Delta);
	float ValveThreshold = 0.05f;
	float DefaultGateH = 1.5f;

	// ---- picking & helpers ----
	/** Ray-march the height field. OutFaceCell is the cell in front of the face that was hit (differs from OutCell when a cliff side is hit). */
	bool TraceCell(const FVector& Origin, const FVector& Dir, int32& OutCell, FVector& OutHit, int32* OutFaceCell = nullptr) const;
	FVector CellCenter(int32 CellIdx, float Z) const;
	float SurfaceZ(int32 CellIdx) const { return (Sim.Floor(CellIdx) + Sim.Depth[CellIdx]) * Block; }
	FVector MapCenter() const { return FVector(GW * Cell * 0.5f, GH * Cell * 0.5f, 450.f); }

	// ---- stats for the HUD ----
	TArray<int32> ReservoirCells;
	TArray<FGWPlant> Plants;
	float ReservoirVolume() const;
	float ReservoirContamination() const;
	float CropSurvival() const;
	int32 HazardsSurvived = 0;
	float MaxReservoirContamDuringHazard = 0.f;
	double PumpedTotal() const;
	double PumpedBadTotal() const;

	void ResetWaterOnly();
	/** Run the simulation instantly (used to fill the valley before the player sees it, and by the capture script). */
	void FastForward(float SimSeconds);

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GhostMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SelectMID;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SelectISM;
	UStaticMesh* MeshCube() const { return CubeMesh; }

protected:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> TerrainMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> WaterMesh;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY() TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> SkyDome;

	UPROPERTY() TObjectPtr<UTexture2D> TexA0;
	UPROPERTY() TObjectPtr<UTexture2D> TexA1;
	UPROPERTY() TObjectPtr<UTexture2D> TexB;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TerrainMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WaterMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SkyMID;
	UPROPERTY() TObjectPtr<UMaterialInterface> StructMat;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;

	// plants & decor (instanced)
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> TreeISM;		// 0..2 healthy sizes
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> DeadTreeISM;	// 0..2
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CropISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> DeadCropISM;

	TArray<FLinearColor> BufA, BufB;
	int32 CurA = 0;
	int32 SubstepCounter = 0;
	float Accum = 0.f;
	TArray<int32> SourceCells;
	TArray<int32> FarmCells;
	TArray<float> BaseGround;
	bool bTerrainDirty = false;

	void BuildValley();
	void BuildTerrainMesh();
	void BuildWaterMesh();
	void CreateTextures();
	void UploadTextures(bool bShift);
	void ApplyConstraints();
	void StepOnce();
	void UpdateSeason();
	void UpdatePlants(float SimDt);
	void UpdateStructureVisuals(float DeltaSeconds);
	void ApplyPhaseLook();
	void SpawnPlants();
	void SpawnDecor();
	void BuildStructureVisual(FGWStructure& S);
	void RebuildVisual(int32 CellIdx);
	void RebuildAround(int32 CellIdx);
	bool Joined(const FGWStructure& S, int32 Side) const;
	bool SameRun(const FGWStructure& A, const FGWStructure& B) const;
	UStaticMeshComponent* AddPart(FGWStructure& S, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color, float Pattern, float Rough = 0.8f);
	UInstancedStaticMeshComponent* MakeISM(const TCHAR* MeshPath, const TCHAR* MatPath);
	void DrawFlowArrows() const;
};
