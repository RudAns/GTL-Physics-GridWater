#include "GWWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "RenderingThread.h"

namespace
{
	float Hash2(int32 X, int32 Y)
	{
		uint32 H = (uint32)(X * 374761393 + Y * 668265263);
		H = (H ^ (H >> 13)) * 1274126177u;
		return (float)((H ^ (H >> 16)) & 0xffff) / 65535.f;
	}

	float VNoise(float X, float Y)
	{
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const float Fx = X - X0, Fy = Y - Y0;
		const float Sx = Fx * Fx * (3 - 2 * Fx), Sy = Fy * Fy * (3 - 2 * Fy);
		const float A = Hash2(X0, Y0), B = Hash2(X0 + 1, Y0), C = Hash2(X0, Y0 + 1), D = Hash2(X0 + 1, Y0 + 1);
		return FMath::Lerp(FMath::Lerp(A, B, Sx), FMath::Lerp(C, D, Sx), Sy);
	}

	float DistToSeg(FVector2f P, FVector2f A, FVector2f B, float& OutT)
	{
		const FVector2f AB = B - A;
		OutT = FMath::Clamp(FVector2f::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1e-4f), 0.f, 1.f);
		return FVector2f::Distance(P, A + AB * OutT);
	}

	const FLinearColor Wood(0.30f, 0.17f, 0.085f), WoodDark(0.17f, 0.095f, 0.045f), Stone(0.46f, 0.44f, 0.40f),
		Metal(0.24f, 0.26f, 0.28f), Grass(0.20f, 0.34f, 0.10f), Brass(0.62f, 0.45f, 0.16f);

	template <typename T> T* LoadObj(const TCHAR* Path) { return LoadObject<T>(nullptr, Path); }
}

AGWWorld::AGWWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
	TerrainMesh->SetupAttachment(Root);
	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Water"));
	WaterMesh->SetupAttachment(Root);
	WaterMesh->SetCastShadow(false);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetWorldRotation(FRotator(-48.f, -35.f, 0.f));
	Sun->SetIntensity(6.f);
	Sun->SetAtmosphereSunLight(false);
	Sun->SetDynamicShadowDistanceMovableLight(20000.f);
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(Root);
	Post->bUnbound = true;
	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(Root);
	SkyDome->SetCastShadow(false);
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

// =================================================================================================
// setup
// =================================================================================================
void AGWWorld::BeginPlay()
{
	Super::BeginPlay();
	CubeMesh = LoadObj<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylMesh = LoadObj<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObj<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConeMesh = LoadObj<UStaticMesh>(TEXT("/Engine/BasicShapes/Cone.Cone"));
	StructMat = LoadObj<UMaterialInterface>(TEXT("/Game/GridWater/M_Struct.M_Struct"));
	if (UMaterialInterface* G = LoadObj<UMaterialInterface>(TEXT("/Game/GridWater/M_Ghost.M_Ghost")))
	{
		GhostMID = UMaterialInstanceDynamic::Create(G, this);
		SelectMID = UMaterialInstanceDynamic::Create(G, this);
		SelectMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.72f, 0.22f, 0.22f));
		SelectISM = NewObject<UInstancedStaticMeshComponent>(this);
		SelectISM->SetupAttachment(Root);
		SelectISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SelectISM->SetCastShadow(false);
		SelectISM->RegisterComponent();
		SelectISM->SetStaticMesh(CubeMesh);
		SelectISM->SetMaterial(0, SelectMID);
	}

	// lighting look
	if (UTextureCube* Cube = LoadObj<UTextureCube>(TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap")))
	{
		SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
		SkyLight->Cubemap = Cube;
	}
	SkyLight->SetIntensity(2.6f);
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->RecaptureSky();
	FPostProcessSettings& P = Post->Settings;
	P.bOverride_AutoExposureMethod = true; P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true; P.AutoExposureApplyPhysicalCameraExposure = false;
	P.bOverride_AutoExposureBias = true; P.AutoExposureBias = -0.9f;
	P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.25f;
	P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.25f;
	P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	if (UMaterialInterface* SkyMat = LoadObj<UMaterialInterface>(TEXT("/Game/GridWater/M_Sky.M_Sky")))
	{
		SkyDome->SetStaticMesh(SphereMesh);
		SkyMID = UMaterialInstanceDynamic::Create(SkyMat, this);
		SkyDome->SetMaterial(0, SkyMID);
		SkyDome->SetWorldLocation(MapCenter());
		SkyDome->SetWorldScale3D(FVector(800.f));
	}

	BuildValley();
	CreateTextures();
	if (UMaterialInterface* M = LoadObj<UMaterialInterface>(TEXT("/Game/GridWater/M_Terrain.M_Terrain"))) TerrainMID = UMaterialInstanceDynamic::Create(M, this);
	if (UMaterialInterface* M = LoadObj<UMaterialInterface>(TEXT("/Game/GridWater/M_Water.M_Water"))) WaterMID = UMaterialInstanceDynamic::Create(M, this);
	const FLinearColor Grid(GW, GH, Cell, Block);
	for (UMaterialInstanceDynamic* M : { TerrainMID.Get(), WaterMID.Get() })
	{
		if (!M) continue;
		M->SetVectorParameterValue(TEXT("Grid"), Grid);
		M->SetTextureParameterValue(TEXT("TB"), TexB);
	}
	BuildTerrainMesh();
	BuildWaterMesh();

	// default structures: an automatic valve across the reservoir branch and a pump on the reservoir shore
	for (int32 Y = 0; Y < GH; ++Y)
	{
		const int32 I = Sim.Idx(54, Y);
		if (FarmCells.Contains(I)) continue;
		if (Sim.Ground[I] < BaseGround[I] - 0.5f && Y > 24 && Y < 40) Place(EGWTool::Valve, I, 0);
	}
	int32 PumpCell = INDEX_NONE; float BestX = -1;
	for (int32 I : ReservoirCells) if (I % GW > BestX && Sim.Ground[I + 1] >= 5.f) { BestX = I % GW; PumpCell = I + 1; }
	if (PumpCell != INDEX_NONE) Place(EGWTool::Pump, PumpCell, 0);

	FastForward(DayLen * 4.f);		// let rivers, the reservoir and the irrigation settle before the player looks
	SpawnPlants();
	SpawnDecor();
	SimTime = 0.f; PhaseStart = 0.f; Phase = EGWPhase::Temperate; Cycle = 1;
	Sim.ResetLedger();
	UploadTextures(true); UploadTextures(true);
	ApplyPhaseLook();
}

void AGWWorld::BuildValley()
{
	Sim.Init(GW, GH);
	BaseGround.SetNum(Sim.N);
	const FVector2f ResC(75.f, 19.f);
	for (int32 Y = 0; Y < GH; ++Y)
		for (int32 X = 0; X < GW; ++X)
		{
			float B = 9.4f - Y * 0.085f - X * 0.034f + 1.3f * (VNoise(X * 0.085f, Y * 0.085f) - 0.5f) + 0.6f * (VNoise(X * 0.21f + 17.f, Y * 0.21f + 5.f) - 0.5f);
			const float Dr = FVector2f::Distance(FVector2f(X, Y), ResC);
			if (Dr < 13.f || (X >= 80 && Y >= 6 && Y <= 34)) B = FMath::Max(B, 5.6f);		// reservoir rim and the east farm plateau
			if (X >= 80 && Y >= 6 && Y <= 34) B = 5.5f;
			BaseGround[Sim.Idx(X, Y)] = FMath::Clamp(FMath::FloorToFloat(B), 1.f, 11.f);
		}
	for (int32 I = 0; I < Sim.N; ++I) Sim.Ground[I] = BaseGround[I];

	// river: carve along polylines with a bed that only ever steps down (terrace drops become waterfalls)
	struct FPath { TArray<FVector2f> P; float Width; };
	const FPath Main{ { {13, 5}, {16, 9}, {21, 13}, {27, 17}, {33, 20}, {38, 24}, {42, 28}, {46, 31}, {48, 33} }, 2.1f };
	const FPath East{ { {48, 33}, {53, 32}, {58, 30}, {63, 27}, {67, 24}, {71, 21} }, 1.5f };
	const FPath South{ { {48, 33}, {47, 38}, {44, 43}, {42, 48}, {44, 53}, {48, 58}, {52, 63}, {54, 67}, {55, 72} }, 1.7f };
	TArray<float> Carve; Carve.Init(1000.f, Sim.N);
	auto CarvePath = [&](const FPath& Path, float StartBed) -> float
	{
		float Bed = StartBed;
		for (int32 S = 0; S + 1 < Path.P.Num(); ++S)
		{
			const FVector2f A = Path.P[S], B = Path.P[S + 1];
			const int32 Steps = FMath::CeilToInt(FVector2f::Distance(A, B) * 4.f);
			for (int32 K = 0; K <= Steps; ++K)
			{
				const FVector2f Q = FMath::Lerp(A, B, (float)K / Steps);
				const int32 Qx = FMath::Clamp(FMath::RoundToInt(Q.X), 0, GW - 1), Qy = FMath::Clamp(FMath::RoundToInt(Q.Y), 0, GH - 1);
				Bed = FMath::Min(Bed, BaseGround[Sim.Idx(Qx, Qy)] - 2.f);
				const int32 R = FMath::CeilToInt(Path.Width) + 1;
				for (int32 Oy = -R; Oy <= R; ++Oy)
					for (int32 Ox = -R; Ox <= R; ++Ox)
					{
						const int32 X = Qx + Ox, Y = Qy + Oy;
						if (!Sim.In(X, Y)) continue;
						if (FVector2f::Distance(FVector2f(X, Y), Q) <= Path.Width) Carve[Sim.Idx(X, Y)] = FMath::Min(Carve[Sim.Idx(X, Y)], FMath::Max(Bed, 0.f));
					}
			}
		}
		return Bed;
	};
	const float ForkBed = CarvePath(Main, BaseGround[Sim.Idx(13, 5)] - 2.f);
	const float EastBed = CarvePath(East, ForkBed);
	CarvePath(South, ForkBed);
	// spring pool and reservoir basin
	for (int32 Y = 0; Y < GH; ++Y)
		for (int32 X = 0; X < GW; ++X)
		{
			const int32 I = Sim.Idx(X, Y);
			const float Ds = FVector2f::Distance(FVector2f(X, Y), FVector2f(13, 5));
			if (Ds < 2.6f) Carve[I] = FMath::Min(Carve[I], BaseGround[Sim.Idx(13, 5)] - 2.f);
			const float Dr = FVector2f::Distance(FVector2f(X, Y), ResC);
			if (Dr < 7.2f) { Carve[I] = FMath::Min(Carve[I], FMath::Max(1.f, EastBed - (Dr < 5.f ? 2.f : 1.f))); ReservoirCells.Add(I); }
		}
	for (int32 I = 0; I < Sim.N; ++I) if (Carve[I] < 999.f) Sim.Ground[I] = FMath::Min(Sim.Ground[I], Carve[I]);

	// south farm: a flat field one step above the river banks
	float SouthBed = 100.f;
	for (int32 Y = 50; Y <= 60; ++Y) for (int32 X = 30; X < 60; ++X) if (Carve[Sim.Idx(X, Y)] < 999.f) SouthBed = FMath::Min(SouthBed, Sim.Ground[Sim.Idx(X, Y)]);
	for (int32 Y = 51; Y <= 59; ++Y)
		for (int32 X = 33; X <= 40; ++X)
		{
			const int32 I = Sim.Idx(X, Y);
			if (Carve[I] < 999.f) continue;
			Sim.Ground[I] = BaseGround[I] = SouthBed + 1.f;
			FarmCells.Add(I);
		}
	// east farm: one step above the reservoir inflow, right next to the basin, so the reservoir irrigates it
	const float FarmLevel = EastBed + 1.f;
	for (int32 Y = 0; Y < GH; ++Y)
		for (int32 X = 0; X < GW; ++X)
		{
			const int32 I = Sim.Idx(X, Y);
			const float Dr = FVector2f::Distance(FVector2f(X, Y), ResC);
			if (Dr >= 7.2f && Dr < 10.f && Carve[I] > 999.f) Sim.Ground[I] = BaseGround[I] = FMath::Max(Sim.Ground[I], FarmLevel);
		}
	for (int32 Y = 9; Y <= 31; ++Y)
		for (int32 X = 83; X <= 92; ++X) { const int32 I = Sim.Idx(X, Y); Sim.Ground[I] = BaseGround[I] = FarmLevel; FarmCells.Add(I); }

	// the river leaves the map at the bottom edge
	for (int32 X = 0; X < GW; ++X) { const int32 I = Sim.Idx(X, GH - 1); if (Carve[I] < 999.f) Sim.Drain[I] = 1; }
	SourceCells = { Sim.Idx(12, 5), Sim.Idx(13, 5), Sim.Idx(14, 5) };
	ApplyConstraints();
}

void AGWWorld::CreateTextures()
{
	auto Make = [](int32 W, int32 H) {
		UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_A32B32G32R32F);
		T->SRGB = false; T->Filter = TF_Bilinear; T->AddressX = TA_Clamp; T->AddressY = TA_Clamp;
		T->CompressionSettings = TC_HDR; T->NeverStream = true;
		T->UpdateResource();
		return T;
	};
	TexA0 = Make(GW, GH); TexA1 = Make(GW, GH); TexB = Make(GW, GH);
	BufA.SetNumZeroed(Sim.N); BufB.SetNumZeroed(Sim.N);
}

void AGWWorld::UploadTextures(bool bShift)
{
	if (!TexA0) return;
	for (int32 I = 0; I < Sim.N; ++I)
	{
		const float D = Sim.Depth[I];
		BufA[I] = FLinearColor(Sim.Floor(I) + D, D, Sim.Conc[I], Sim.Sat[I]);
		BufB[I] = FLinearColor(Sim.VelX[I], Sim.VelY[I], Sim.Moist[I], Sim.SoilC[I]);
	}
	if (bShift) CurA ^= 1;
	UTexture2D* Target = CurA ? TexA1 : TexA0;
	auto Upload = [this](UTexture2D* T, const TArray<FLinearColor>& Src)
	{
		const int32 Bytes = Src.Num() * sizeof(FLinearColor);
		uint8* Copy = (uint8*)FMemory::Malloc(Bytes);
		FMemory::Memcpy(Copy, Src.GetData(), Bytes);
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, GW, GH);
		T->UpdateTextureRegions(0, 1, Region, GW * sizeof(FLinearColor), sizeof(FLinearColor), Copy,
			[](uint8* Data, const FUpdateTextureRegion2D* R) { FMemory::Free(Data); delete R; });
	};
	Upload(Target, BufA);
	Upload(TexB, BufB);
	UTexture2D* PrevT = CurA ? TexA0 : TexA1;
	for (UMaterialInstanceDynamic* M : { TerrainMID.Get(), WaterMID.Get() })
	{
		if (!M) continue;
		M->SetTextureParameterValue(TEXT("TA0"), PrevT);
		M->SetTextureParameterValue(TEXT("TA1"), Target);
	}
}

// =================================================================================================
// meshes
// =================================================================================================
void AGWWorld::BuildTerrainMesh()
{
	TArray<FVector> V; TArray<int32> T; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FLinearColor> C; TArray<FProcMeshTangent> Tan;
	V.Reserve(Sim.N * 12);
	auto Quad = [&](FVector A, FVector B, FVector Cc, FVector D, FVector N, FVector2D Uv, FLinearColor Ca, FLinearColor Cb, FLinearColor Cc2, FLinearColor Cd)
	{
		const int32 Base = V.Num();
		V.Append({ A, B, Cc, D });
		Nrm.Append({ N, N, N, N });
		UV.Append({ Uv, Uv, Uv, Uv });
		C.Append({ Ca, Cb, Cc2, Cd });
		T.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
	};
	const float Bottom = -2.5f * Block;
	for (int32 Y = 0; Y < GH; ++Y)
		for (int32 X = 0; X < GW; ++X)
		{
			const int32 I = Sim.Idx(X, Y);
			const float G = Sim.Ground[I], Z = G * Block, X0 = X * Cell, Y0 = Y * Cell, X1 = X0 + Cell, Y1 = Y0 + Cell;
			const FVector2D Uv((X + 0.5f) / GW, (Y + 0.5f) / GH);
			const float Hs = Hash2(X, Y);
			auto Ao = [&](int32 Sx, int32 Sy)
			{
				float A = 1.f;
				for (const FIntPoint O : { FIntPoint(Sx, 0), FIntPoint(0, Sy), FIntPoint(Sx, Sy) })
					if (Sim.In(X + O.X, Y + O.Y) && Sim.Ground[Sim.Idx(X + O.X, Y + O.Y)] > G) A -= 0.17f;
				return FLinearColor(1.f, Hs, A, 0.f);
			};
			Quad(FVector(X0, Y0, Z), FVector(X1, Y0, Z), FVector(X1, Y1, Z), FVector(X0, Y1, Z), FVector::UpVector, Uv,
				Ao(-1, -1), Ao(1, -1), Ao(1, 1), Ao(-1, 1));
			for (int32 K = 0; K < 4; ++K)
			{
				const int32 NX = X + FWaterSim::DX[K], NY = Y + FWaterSim::DY[K];
				const float Nz = Sim.In(NX, NY) ? Sim.Ground[Sim.Idx(NX, NY)] * Block : Bottom;
				if (Nz >= Z) continue;
				FVector A, B;
				switch (K)
				{
				case 0: A = FVector(X1, Y0, 0); B = FVector(X1, Y1, 0); break;
				case 1: A = FVector(X1, Y1, 0); B = FVector(X0, Y1, 0); break;
				case 2: A = FVector(X0, Y1, 0); B = FVector(X0, Y0, 0); break;
				default: A = FVector(X0, Y0, 0); B = FVector(X1, Y0, 0); break;
				}
				const FVector N(FWaterSim::DX[K], FWaterSim::DY[K], 0);
				const FLinearColor Top(0.f, Hs, 1.f, 0.f), Low(0.f, Hs, Nz <= Bottom ? 0.8f : 0.55f, 0.f);
				Quad(A + FVector(0, 0, Z), B + FVector(0, 0, Z), B + FVector(0, 0, Nz), A + FVector(0, 0, Nz), N, Uv, Top, Top, Low, Low);
			}
		}
	TerrainMesh->CreateMeshSection_LinearColor(0, V, T, Nrm, UV, C, Tan, false);
	if (TerrainMID) TerrainMesh->SetMaterial(0, TerrainMID);
	bTerrainDirty = false;
}

void AGWWorld::BuildWaterMesh()
{
	// Static topology; heights come from the data textures in the material's world-position offset (report ch.11.2).
	TArray<FVector> V; TArray<int32> T; TArray<FVector> Nrm; TArray<FVector2D> UV0, UV1, UV2, UV3; TArray<FLinearColor> C; TArray<FProcMeshTangent> Tan;
	V.Reserve(Sim.N * 20);
	static const FIntPoint Corners[4] = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
	// side k uses corner pair (a, b) on that edge
	static const int32 SideA[4] = { 1, 2, 3, 0 }, SideB[4] = { 2, 3, 0, 1 };
	for (int32 Y = 0; Y < GH; ++Y)
		for (int32 X = 0; X < GW; ++X)
		{
			const FVector2D Uv((X + 0.5f) / GW, (Y + 0.5f) / GH);
			const float Cx = (X + 0.5f) * Cell, Cy = (Y + 0.5f) * Cell;
			auto Pos = [&](int32 Ci) { return FVector(Cx + Corners[Ci].X * Cell * 0.5f, Cy + Corners[Ci].Y * Cell * 0.5f, 0.f); };
			int32 Base = V.Num();
			for (int32 Ci = 0; Ci < 4; ++Ci)
			{
				V.Add(Pos(Ci)); Nrm.Add(FVector::UpVector); UV0.Add(Uv); UV1.Add(FVector2D(0, 0));
				UV2.Add(FVector2D(Corners[Ci].X, Corners[Ci].Y)); UV3.Add(FVector2D(0, 0)); C.Add(FLinearColor::White);
			}
			T.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
			for (int32 K = 0; K < 4; ++K)
			{
				Base = V.Num();
				const int32 Ca = SideA[K], Cb = SideB[K];
				const FVector N(FWaterSim::DX[K], FWaterSim::DY[K], 0);
				const int32 Ids[4] = { Ca, Cb, Cb, Ca };
				const float Roles[4] = { 1, 1, 2, 2 };
				for (int32 Q = 0; Q < 4; ++Q)
				{
					V.Add(Pos(Ids[Q])); Nrm.Add(N); UV0.Add(Uv); UV1.Add(FVector2D(Roles[Q], 0));
					UV2.Add(FVector2D(Corners[Ids[Q]].X, Corners[Ids[Q]].Y)); UV3.Add(FVector2D(FWaterSim::DX[K], FWaterSim::DY[K])); C.Add(FLinearColor::White);
				}
				T.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			}
		}
	// a degenerate triangle that stretches the bounds over the height the offsets can reach
	const int32 B = V.Num();
	for (const FVector& P : { FVector(0, 0, -400), FVector(GW * Cell, GH * Cell, 1600), FVector(0, 0, -400) })
	{
		V.Add(P); Nrm.Add(FVector::UpVector); UV0.Add(FVector2D(0.5, 0.5)); UV1.Add(FVector2D(3, 0)); UV2.Add(FVector2D(0, 0)); UV3.Add(FVector2D(0, 0)); C.Add(FLinearColor::White);
	}
	T.Append({ B, B + 1, B + 2 });
	WaterMesh->CreateMeshSection_LinearColor(0, V, T, Nrm, UV0, UV1, UV2, UV3, C, Tan, false);
	if (WaterMID) WaterMesh->SetMaterial(0, WaterMID);
}

// =================================================================================================
// simulation loop
// =================================================================================================
void AGWWorld::ApplyConstraints()
{
	for (int32 I = 0; I < Sim.N; ++I) { Sim.Solid[I] = 0.f; Sim.InflowLimit[I] = -1.f; Sim.DirLimit[I] = -1; Sim.Blocked[I] = 0; }
	for (const FGWStructure& S : Structures)
	{
		const int32 I = S.Cell;
		switch (S.Type)
		{
		case EGWTool::Levee: Sim.Solid[I] = (float)S.LeveeH; break;
		case EGWTool::Dam: Sim.InflowLimit[I] = 0.5f; break;
		case EGWTool::Floodgate:
		{
			const float Whole = FMath::FloorToFloat(S.GateH + 1e-3f), Frac = S.GateH - Whole;
			Sim.Solid[I] = Whole;
			Sim.InflowLimit[I] = Frac > 0.01f ? Frac : -1.f;
			break;
		}
		case EGWTool::Valve: Sim.DirLimit[I] = (int8)S.Dir; Sim.Blocked[I] = S.bClosed ? 1 : 0; break;
		default: break;
		}
	}
}

void AGWWorld::StepOnce()
{
	// automatic valves read the contamination of the cell upstream (rounded to 0.01, as the game does)
	bool bChanged = false;
	for (FGWStructure& S : Structures)
	{
		if (S.Type == EGWTool::Valve && S.bAuto)
		{
			const int32 X = S.Cell % GW - FWaterSim::DX[S.Dir], Y = S.Cell / GW - FWaterSim::DY[S.Dir];
			const float C = Sim.In(X, Y) ? FMath::RoundToFloat(Sim.Conc[Sim.Idx(X, Y)] * 100.f) / 100.f : 0.f;
			const bool bClose = C > ValveThreshold;
			if (bClose != S.bClosed) { S.bClosed = bClose; bChanged = true; }
		}
		else if (S.Type == EGWTool::Pump)
		{
			int32 Best = S.Cell; float BestD = Sim.Depth[S.Cell];
			for (int32 K = 0; K < 4; ++K)
			{
				const int32 X = S.Cell % GW + FWaterSim::DX[K], Y = S.Cell / GW + FWaterSim::DY[K];
				if (Sim.In(X, Y) && Sim.Depth[Sim.Idx(X, Y)] > BestD) { Best = Sim.Idx(X, Y); BestD = Sim.Depth[Best]; }
			}
			float Bad = 0.f;
			const float Got = Sim.RemoveWater(Best, 0.45f * SubDt, &Bad);
			S.Pumped += Got; S.PumpedBad += Bad;
			S.Anim += Got > 0.f ? 1.f : 0.f;
		}
	}
	if (bChanged) ApplyConstraints();
	UpdateSeason();
	if (SourceModifier > 0.f)
		for (int32 I : SourceCells) Sim.AddWater(I, SourceStrength * SourceModifier * SubDt / SourceCells.Num(), SourceContamination);
	Sim.Substep(SubDt);
	SimTime += SubDt;
	if (++SubstepCounter % 2 == 0)
	{
		Sim.UpdateDerived(SubDt * 2.f);
		UpdatePlants(SubDt * 2.f);
		if (Phase != EGWPhase::Temperate) MaxReservoirContamDuringHazard = FMath::Max(MaxReservoirContamDuringHazard, ReservoirContamination());
	}
}

void AGWWorld::FastForward(float SimSeconds)
{
	const int32 Steps = FMath::CeilToInt(SimSeconds / SubDt);
	for (int32 K = 0; K < Steps; ++K) StepOnce();
}

void AGWWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bTerrainDirty) BuildTerrainMesh();
	Accum += FMath::Min(DeltaSeconds, 0.1f) * 10.f * SpeedFactor();	// 10 substeps (3 simulated seconds) per real second at x1
	int32 Done = 0;
	while (Accum >= 1.f && Done < 8) { StepOnce(); UploadTextures(true); Accum -= 1.f; ++Done; }
	if (Accum > 1.f) Accum = 1.f;
	const float Alpha = FMath::Clamp(Accum, 0.f, 1.f);
	for (UMaterialInstanceDynamic* M : { TerrainMID.Get(), WaterMID.Get() })
		if (M) M->SetScalarParameterValue(TEXT("Alpha"), Alpha);
	UpdateStructureVisuals(DeltaSeconds);
	if (bFlowArrows) DrawFlowArrows();
}

// =================================================================================================
// seasons
// =================================================================================================
float AGWWorld::PhaseDaysLeft() const { return PhaseLengthDays() - DayOfPhase(); }

void AGWWorld::UpdateSeason()
{
	if (SimTime - PhaseStart >= PhaseLengthDays() * DayLen) AdvancePhase();
	const float T = DayOfPhase(), Left = PhaseDaysLeft();
	static EGWPhase PrevHazard = EGWPhase::Temperate;
	SourceModifier = 1.f; SourceContamination = 0.f;
	if (Phase == EGWPhase::Temperate)
	{
		// sources fade out half a day before a drought and fade back in after one (report ch.04)
		if (NextHazard == EGWPhase::Drought && Left < 0.5f) SourceModifier = FMath::Max(0.f, Left / 0.5f);
		if (PrevHazard == EGWPhase::Drought && T < 0.5f) SourceModifier = FMath::Min(SourceModifier, T / 0.5f);
	}
	else if (Phase == EGWPhase::Drought) { SourceModifier = 0.f; PrevHazard = EGWPhase::Drought; }
	else
	{
		PrevHazard = EGWPhase::Badtide;
		auto Ramp = [](float X) { const float A = 17.f * (X - 0.5f); return 0.5f + 0.5f * (2.f / (FMath::Exp(A) + FMath::Exp(-A))); };
		SourceContamination = T < 0.5f ? Ramp(T) : Left < 0.5f ? Ramp(Left) : 1.f;
	}
}

void AGWWorld::AdvancePhase()
{
	if (Phase == EGWPhase::Temperate)
	{
		Phase = NextHazard;
		MaxReservoirContamDuringHazard = 0.f;
	}
	else
	{
		NextHazard = Phase == EGWPhase::Badtide ? EGWPhase::Drought : EGWPhase::Badtide;
		Phase = EGWPhase::Temperate;
		++Cycle; ++HazardsSurvived;
		// beavers replant: dead crops and trees come back at the start of each temperate season
		for (FGWPlant& P : Plants)
			if (P.bDead)
			{
				P.bDead = false; P.Health = 0.6f;
				UInstancedStaticMeshComponent* A = P.Kind == 0 ? TreeISM[P.Size].Get() : CropISM.Get();
				UInstancedStaticMeshComponent* B = P.Kind == 0 ? DeadTreeISM[P.Size].Get() : DeadCropISM.Get();
				FTransform Z = P.Xf; Z.SetScale3D(FVector(0.001f));
				A->UpdateInstanceTransform(P.Inst, P.Xf, true, true);
				B->UpdateInstanceTransform(P.Inst, Z, true, true);
			}
	}
	PhaseStart = SimTime;
	ApplyPhaseLook();
}

void AGWWorld::ApplyPhaseLook()
{
	FLinearColor SunC(1.f, 0.96f, 0.88f), FogC(0.55f, 0.66f, 0.80f), SkyTop(0.22f, 0.45f, 0.80f), SkyHz(0.70f, 0.80f, 0.90f);
	float FogD = 0.006f;
	if (Phase == EGWPhase::Drought) { SunC = FLinearColor(1.f, 0.84f, 0.62f); FogC = FLinearColor(0.80f, 0.68f, 0.50f); SkyTop = FLinearColor(0.45f, 0.48f, 0.55f); SkyHz = FLinearColor(0.92f, 0.78f, 0.58f); FogD = 0.012f; }
	if (Phase == EGWPhase::Badtide) { SunC = FLinearColor(1.f, 0.80f, 0.72f); FogC = FLinearColor(0.62f, 0.45f, 0.42f); SkyTop = FLinearColor(0.30f, 0.26f, 0.36f); SkyHz = FLinearColor(0.80f, 0.55f, 0.48f); FogD = 0.010f; }
	Sun->SetLightColor(SunC);
	Fog->SetFogInscatteringColor(FogC);
	Fog->SetFogDensity(FogD);
	Fog->SetFogHeightFalloff(0.002f);
	Fog->SetStartDistance(6000.f);
	if (SkyMID) { SkyMID->SetVectorParameterValue(TEXT("Top"), SkyTop); SkyMID->SetVectorParameterValue(TEXT("Horizon"), SkyHz); }
}

// =================================================================================================
// structures
// =================================================================================================
int32 AGWWorld::FindStructure(int32 CellIdx) const
{
	return Structures.IndexOfByPredicate([CellIdx](const FGWStructure& S) { return S.Cell == CellIdx; });
}

bool AGWWorld::CanPlace(EGWTool Tool, int32 CellIdx, FString* Why) const
{
	auto No = [Why](const TCHAR* Msg) { if (Why) *Why = Msg; return false; };
	if (CellIdx < 0 || CellIdx >= Sim.N) return No(TEXT("超出地图"));
	if (SourceCells.Contains(CellIdx)) return No(TEXT("水源格不能建造"));
	if (Tool == EGWTool::Erase) return FindStructure(CellIdx) != INDEX_NONE ? true : No(TEXT("这里没有建筑"));
	if (FindStructure(CellIdx) != INDEX_NONE) return No(TEXT("已有建筑"));
	if (Tool == EGWTool::Dig && Sim.Ground[CellIdx] <= 0.f) return No(TEXT("已到最低高度"));
	if (Tool == EGWTool::Raise && Sim.Ground[CellIdx] >= 11.f) return No(TEXT("已到最高高度"));
	if (Tool == EGWTool::Pump)
	{
		bool bNear = Sim.Depth[CellIdx] > 0.05f;
		for (int32 K = 0; K < 4 && !bNear; ++K)
		{
			const int32 X = CellIdx % GW + FWaterSim::DX[K], Y = CellIdx / GW + FWaterSim::DY[K];
			bNear = Sim.In(X, Y) && Sim.Depth[Sim.Idx(X, Y)] > 0.05f;
		}
		if (!bNear) return No(TEXT("水泵需要建在水边"));
	}
	return true;
}

bool AGWWorld::Place(EGWTool Tool, int32 CellIdx, int32 Dir, float GateH)
{
	if (!CanPlace(Tool, CellIdx)) return false;
	if (Tool == EGWTool::Erase) return RemoveAt(CellIdx);
	if (Tool == EGWTool::Dig || Tool == EGWTool::Raise) return Terraform(CellIdx, Tool == EGWTool::Dig ? -1 : 1);
	FGWStructure S;
	S.Type = Tool; S.Cell = CellIdx; S.Dir = Dir & 3; S.GateH = GateH;
	if (Tool == EGWTool::Levee) Sim.RemoveWater(CellIdx, Sim.Depth[CellIdx]);	// the levee displaces the water standing in its cell
	// a new piece joining an existing run takes the run's settings (synchronised, as in the game)
	for (int32 K = 0; K < 4; ++K)
	{
		const int32 X = CellIdx % GW + FWaterSim::DX[K], Y = CellIdx / GW + FWaterSim::DY[K];
		const int32 N = Sim.In(X, Y) ? FindStructure(Sim.Idx(X, Y)) : INDEX_NONE;
		if (N == INDEX_NONE || !SameRun(S, Structures[N])) continue;
		S.GateH = Structures[N].GateH; S.LeveeH = Structures[N].LeveeH; S.bAuto = Structures[N].bAuto; S.bClosed = Structures[N].bClosed;
		break;
	}
	Structures.Add(S);
	RebuildAround(CellIdx);
	ApplyConstraints();
	return true;
}

bool AGWWorld::RemoveAt(int32 CellIdx)
{
	const int32 Index = FindStructure(CellIdx);
	if (Index == INDEX_NONE) return false;
	for (UStaticMeshComponent* C : Structures[Index].Parts) if (C) C->DestroyComponent();
	Structures.RemoveAt(Index);
	Selected.Remove(CellIdx);
	RebuildAround(CellIdx);
	ApplyConstraints();
	RefreshSelectionVisual();
	return true;
}

void AGWWorld::AdjustGate(int32 CellIdx, float Delta)
{
	const int32 Index = FindStructure(CellIdx);
	if (Index == INDEX_NONE || Structures[Index].Type != EGWTool::Floodgate) return;
	Structures[Index].GateH = FMath::Clamp(FMath::RoundToFloat((Structures[Index].GateH + Delta) * 20.f) / 20.f, 0.f, 3.f);
	ApplyConstraints();
}

void AGWWorld::ToggleValve(int32 CellIdx)
{
	const int32 Index = FindStructure(CellIdx);
	if (Index == INDEX_NONE || Structures[Index].Type != EGWTool::Valve) return;
	FGWStructure& S = Structures[Index];
	// cycle: auto -> forced open -> forced closed -> auto
	if (S.bAuto) { S.bAuto = false; S.bClosed = false; }
	else if (!S.bClosed) S.bClosed = true;
	else S.bAuto = true;
	ApplyConstraints();
}

bool AGWWorld::SameRun(const FGWStructure& A, const FGWStructure& B) const
{
	if (A.Type != B.Type) return false;
	if (A.Type == EGWTool::Levee) return true;
	if (A.Type == EGWTool::Floodgate || A.Type == EGWTool::Valve || A.Type == EGWTool::Dam) return (A.Dir & 1) == (B.Dir & 1);
	return false;
}

bool AGWWorld::Joined(const FGWStructure& S, int32 Side) const
{
	// Side +-1 along the structure's cross-channel axis (local Y): grid offset (-DY, DX) of its direction
	const int32 X = S.Cell % GW - Side * FWaterSim::DY[S.Dir], Y = S.Cell / GW + Side * FWaterSim::DX[S.Dir];
	const int32 N = Sim.In(X, Y) ? FindStructure(Sim.Idx(X, Y)) : INDEX_NONE;
	return N != INDEX_NONE && SameRun(S, Structures[N]);
}

TArray<int32> AGWWorld::ConnectedGroup(int32 CellIdx) const
{
	TArray<int32> Out;
	const int32 Start = FindStructure(CellIdx);
	if (Start == INDEX_NONE) return Out;
	TArray<int32> Open = { CellIdx };
	TSet<int32> Seen = { CellIdx };
	while (Open.Num())
	{
		const int32 C = Open.Pop();
		Out.Add(C);
		for (int32 K = 0; K < 4; ++K)
		{
			const int32 X = C % GW + FWaterSim::DX[K], Y = C / GW + FWaterSim::DY[K];
			if (!Sim.In(X, Y)) continue;
			const int32 N = Sim.Idx(X, Y), Si = FindStructure(N);
			if (Si == INDEX_NONE || Seen.Contains(N) || !SameRun(Structures[Start], Structures[Si])) continue;
			Seen.Add(N); Open.Add(N);
		}
	}
	return Out;
}

void AGWWorld::AdjustGroupHeight(const TArray<int32>& Cells, int32 Primary, int32 Steps)
{
	const int32 P = FindStructure(Primary != INDEX_NONE ? Primary : (Cells.Num() ? Cells[0] : INDEX_NONE));
	if (P == INDEX_NONE) return;
	const FGWStructure& Ref = Structures[P];
	if (Ref.Type == EGWTool::Floodgate)
	{
		const float H = FMath::Clamp(FMath::RoundToFloat((Ref.GateH + Steps * 0.25f) * 20.f) / 20.f, 0.f, 3.f);
		for (int32 C : Cells) { const int32 I = FindStructure(C); if (I != INDEX_NONE && Structures[I].Type == EGWTool::Floodgate) Structures[I].GateH = H; }
	}
	else if (Ref.Type == EGWTool::Levee)
	{
		const int32 H = FMath::Clamp(Ref.LeveeH + Steps, 1, 3);
		for (int32 C : Cells) { const int32 I = FindStructure(C); if (I != INDEX_NONE && Structures[I].Type == EGWTool::Levee && Structures[I].LeveeH != H) { Structures[I].LeveeH = H; RebuildVisual(C); } }
	}
	ApplyConstraints();
	RefreshSelectionVisual();
}

void AGWWorld::SetValveMode(const TArray<int32>& Cells, int32 Mode)
{
	for (int32 C : Cells)
	{
		const int32 I = FindStructure(C);
		if (I == INDEX_NONE || Structures[I].Type != EGWTool::Valve) continue;
		Structures[I].bAuto = Mode == 0;
		Structures[I].bClosed = Mode == 2;
	}
	ApplyConstraints();
}

void AGWWorld::SetSelection(int32 CellIdx)
{
	Selected = CellIdx != INDEX_NONE ? ConnectedGroup(CellIdx) : TArray<int32>();
	SelectedPrimary = Selected.Num() ? CellIdx : INDEX_NONE;
	RefreshSelectionVisual();
}

void AGWWorld::RefreshSelectionVisual()
{
	if (!SelectISM) return;
	SelectISM->ClearInstances();
	for (int32 C : Selected)
	{
		const int32 I = FindStructure(C);
		if (I == INDEX_NONE) continue;
		const FGWStructure& S = Structures[I];
		const float H = S.Type == EGWTool::Levee ? S.LeveeH : S.Type == EGWTool::Floodgate ? 3.6f : S.Type == EGWTool::Valve ? 1.8f : 0.9f;
		SelectISM->AddInstance(FTransform(FRotator::ZeroRotator, CellCenter(C, Sim.Ground[C] * Block + H * 50.f), FVector(1.06f, 1.06f, H + 0.08f)), true);
	}
}

void AGWWorld::RebuildVisual(int32 CellIdx)
{
	const int32 I = FindStructure(CellIdx);
	if (I == INDEX_NONE) return;
	FGWStructure& S = Structures[I];
	for (UStaticMeshComponent* C : S.Parts) if (C) C->DestroyComponent();
	S.Parts.Reset(); S.Moving = nullptr; S.Lamp = nullptr;
	BuildStructureVisual(S);
}

void AGWWorld::RebuildAround(int32 CellIdx)
{
	RebuildVisual(CellIdx);
	for (int32 K = 0; K < 4; ++K)
	{
		const int32 X = CellIdx % GW + FWaterSim::DX[K], Y = CellIdx / GW + FWaterSim::DY[K];
		if (Sim.In(X, Y)) RebuildVisual(Sim.Idx(X, Y));
	}
}

bool AGWWorld::Terraform(int32 CellIdx, int32 Delta)
{
	if (FindStructure(CellIdx) != INDEX_NONE) return false;
	const float NewG = FMath::Clamp(Sim.Ground[CellIdx] + Delta, 0.f, 11.f);
	if (NewG == Sim.Ground[CellIdx]) return false;
	if (Delta > 0) Sim.RemoveWater(CellIdx, FMath::Min(Sim.Depth[CellIdx], 1.f));	// raised earth pushes water out
	Sim.Ground[CellIdx] = NewG;
	bTerrainDirty = true;
	return true;
}

UStaticMeshComponent* AGWWorld::AddPart(FGWStructure& S, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color, float Pattern, float Rough)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(Root);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	C->SetWorldLocationAndRotation(Loc, Rot);
	C->SetWorldScale3D(Scale);
	if (StructMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(StructMat, C);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		M->SetScalarParameterValue(TEXT("Pattern"), Pattern);
		M->SetScalarParameterValue(TEXT("Rough"), Rough);
		C->SetMaterial(0, M);
	}
	S.Parts.Add(C);
	return C;
}

void AGWWorld::BuildStructureVisual(FGWStructure& S)
{
	const float G = Sim.Ground[S.Cell] * Block;
	const FVector C = CellCenter(S.Cell, G);
	const FRotator R(0.f, S.Dir * 90.f, 0.f);					// local X = flow direction
	const FVector Fw = R.Vector(), Rt = FRotationMatrix(R).GetUnitAxis(EAxis::Y);
	switch (S.Type)
	{
	case EGWTool::Levee:
		AddPart(S, CubeMesh, C + FVector(0, 0, S.LeveeH * 50.f), R, FVector(1.0f, 1.0f, (float)S.LeveeH), Stone, 2.f);
		break;
	case EGWTool::Dam:
		for (int32 K = 0; K < 6; ++K)
			AddPart(S, CylMesh, C + Rt * (-42.f + K * 16.8f) + FVector(0, 0, 32), R, FVector(0.17f, 0.17f, 0.64f + 0.05f * Hash2(S.Cell, K)), Wood, 1.f);
		AddPart(S, CubeMesh, C + Fw * -9.f + FVector(0, 0, 44), R, FVector(0.08f, 1.02f, 0.12f), WoodDark, 1.f);
		AddPart(S, CubeMesh, C + FVector(0, 0, 4), R, FVector(0.42f, 1.0f, 0.1f), Stone, 2.f);
		break;
	case EGWTool::Floodgate:
	case EGWTool::Valve:
	{
		// a run of joined gates shares its posts: posts only at the open ends, leaf and beam continuous
		const bool bL = Joined(S, -1), bR = Joined(S, 1);
		const float Lo = bL ? -50.f : -38.f, Hi = bR ? 50.f : 38.f;
		const float BLo = bL ? -50.f : -57.f, BHi = bR ? 50.f : 57.f;
		S.LeafW = (Hi - Lo) / 100.f; S.LeafOff = (Hi + Lo) * 0.5f;
		const bool bGate = S.Type == EGWTool::Floodgate;
		const float PostH = bGate ? 3.4f : 1.5f;
		for (int32 Side : { -1, 1 })
			if (!(Side < 0 ? bL : bR))
				AddPart(S, CubeMesh, C + Rt * Side * 46.f + FVector(0, 0, PostH * 50.f), R, FVector(bGate ? 0.22f : 0.24f, 0.16f, PostH), Stone, 2.f);
		if (bGate)
		{
			AddPart(S, CubeMesh, C + Rt * ((BHi + BLo) * 0.5f) + FVector(0, 0, 345), R, FVector(0.3f, (BHi - BLo) / 100.f, 0.18f), WoodDark, 1.f);
			if (!bL) AddPart(S, CylMesh, C + FVector(0, 0, 372), R + FRotator(90, 0, 0), FVector(0.42f, 0.42f, 0.06f), Brass, 0.f, 0.4f);
			S.Moving = AddPart(S, CubeMesh, C + Rt * S.LeafOff + FVector(0, 0, S.GateH * 50.f), R, FVector(0.12f, S.LeafW, FMath::Max(S.GateH, 0.02f)), Wood, 1.f);
			break;
		}
		AddPart(S, CubeMesh, C + Rt * ((BHi + BLo) * 0.5f) + FVector(0, 0, 155), R, FVector(0.34f, (BHi - BLo) / 100.f, 0.2f), Stone, 2.f);
		AddPart(S, ConeMesh, C + FVector(0, 0, 175) + Fw * 6.f, R + FRotator(-90, 0, 0), FVector(0.22f, 0.22f, 0.28f), Brass, 0.f, 0.4f);
		S.Moving = AddPart(S, CubeMesh, C + Rt * S.LeafOff + FVector(0, 0, 150), R, FVector(0.08f, S.LeafW, 1.0f), Metal, 0.f, 0.45f);
		S.Lamp = AddPart(S, SphereMesh, C + FVector(0, 0, 172) - Fw * 14.f + Rt * 28.f, R, FVector(0.14f), FLinearColor(0.1f, 0.9f, 0.3f), 3.f, 0.3f);
		break;
	}
	case EGWTool::Pump:
		AddPart(S, CubeMesh, C + FVector(0, 0, 32), R, FVector(0.72f, 0.72f, 0.64f), Wood, 1.f);
		AddPart(S, CubeMesh, C + FVector(0, 0, 70) + Rt * 18.f, R + FRotator(0, 0, 32), FVector(0.84f, 0.46f, 0.06f), WoodDark, 1.f);
		AddPart(S, CubeMesh, C + FVector(0, 0, 70) - Rt * 18.f, R + FRotator(0, 0, -32), FVector(0.84f, 0.46f, 0.06f), WoodDark, 1.f);
		AddPart(S, CylMesh, C + Fw * 38.f + FVector(0, 0, 10), R, FVector(0.12f, 0.12f, 1.1f), Metal, 0.f, 0.45f);
		S.Moving = AddPart(S, CylMesh, C - Fw * 40.f + FVector(0, 0, 38), R + FRotator(0, 0, 90), FVector(0.55f, 0.55f, 0.07f), Brass, 0.f, 0.4f);
		break;
	default: break;
	}
}

void AGWWorld::UpdateStructureVisuals(float DeltaSeconds)
{
	for (FGWStructure& S : Structures)
	{
		const float G = Sim.Ground[S.Cell] * Block;
		const FVector C = CellCenter(S.Cell, G);
		if (S.Type == EGWTool::Floodgate && S.Moving)
		{
			const float Cur = S.Moving->GetComponentScale().Z;
			const float Tgt = FMath::Max(S.GateH, 0.02f);
			const float H = FMath::FInterpTo(Cur, Tgt, DeltaSeconds, 6.f);
			const FVector Rt = FRotationMatrix(FRotator(0.f, S.Dir * 90.f, 0.f)).GetUnitAxis(EAxis::Y);
			S.Moving->SetWorldScale3D(FVector(0.12f, S.LeafW, H));
			S.Moving->SetWorldLocation(C + Rt * S.LeafOff + FVector(0, 0, H * 50.f));
		}
		else if (S.Type == EGWTool::Valve && S.Moving)
		{
			const float TgtZ = S.bClosed ? 50.f : 128.f;
			const FVector L = S.Moving->GetComponentLocation();
			S.Moving->SetWorldLocation(FVector(L.X, L.Y, FMath::FInterpTo(L.Z, G + TgtZ, DeltaSeconds, 5.f)));
			if (S.Lamp)
				if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(S.Lamp->GetMaterial(0)))
					M->SetVectorParameterValue(TEXT("Color"), S.bClosed ? FLinearColor(1.f, 0.12f, 0.06f) : (S.bAuto ? FLinearColor(0.1f, 0.9f, 0.3f) : FLinearColor(1.f, 0.75f, 0.1f)));
		}
		else if (S.Type == EGWTool::Pump && S.Moving && S.Anim > 0.f)
		{
			S.Moving->SetWorldRotation(FRotator(FMath::Fmod(GetWorld()->GetTimeSeconds() * 90.f * SpeedFactor(), 360.f), S.Dir * 90.f, 90.f));
		}
	}
}

// =================================================================================================
// plants & decor
// =================================================================================================
UInstancedStaticMeshComponent* AGWWorld::MakeISM(const TCHAR* MeshPath, const TCHAR* MatPath)
{
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->NumCustomDataFloats = 2;
	C->RegisterComponent();
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, MeshPath)) C->SetStaticMesh(M);
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, MatPath))
		for (int32 S = 0; S < FMath::Max(1, C->GetNumMaterials()); ++S) C->SetMaterial(S, Mat);
	return C;
}

void AGWWorld::SpawnPlants()
{
	static const TCHAR* Trees[3] = { TEXT("/Game/GridWater/Props/tree_pine_orange_large.tree_pine_orange_large"), TEXT("/Game/GridWater/Props/tree_pine_orange_medium.tree_pine_orange_medium"), TEXT("/Game/GridWater/Props/tree_pine_orange_small.tree_pine_orange_small") };
	static const TCHAR* Dead[3] = { TEXT("/Game/GridWater/Props/tree_dead_large.tree_dead_large"), TEXT("/Game/GridWater/Props/tree_dead_medium.tree_dead_medium"), TEXT("/Game/GridWater/Props/tree_dead_small.tree_dead_small") };
	const TCHAR* Green = TEXT("/Game/GridWater/MI_PropGreen.MI_PropGreen");
	const TCHAR* Plain = TEXT("/Game/GridWater/MI_Prop.MI_Prop");
	for (int32 K = 0; K < 3; ++K) { TreeISM.Add(MakeISM(Trees[K], Green)); DeadTreeISM.Add(MakeISM(Dead[K], Plain)); }
	CropISM = MakeISM(TEXT("/Game/GridWater/Props/pumpkin_orange.pumpkin_orange"), Plain);
	DeadCropISM = MakeISM(TEXT("/Game/GridWater/Props/pumpkin_orange_small.pumpkin_orange_small"), Plain);

	auto Fit = [](UInstancedStaticMeshComponent* C, float WantHeight) -> float {
		const float Hh = C && C->GetStaticMesh() ? C->GetStaticMesh()->GetBounds().BoxExtent.Z * 2.f : 100.f;
		return WantHeight / FMath::Max(Hh, 1.f);
	};
	const float TreeH[3] = { 230.f, 180.f, 135.f };
	auto AddPlant = [&](int32 I, int32 Kind, int32 Size, const FVector& Loc, float Yaw, float H)
	{
		FGWPlant P; P.Cell = I; P.Kind = Kind; P.Size = Size;
		UInstancedStaticMeshComponent* A = Kind == 0 ? TreeISM[Size].Get() : CropISM.Get();
		UInstancedStaticMeshComponent* B = Kind == 0 ? DeadTreeISM[Size].Get() : DeadCropISM.Get();
		P.Xf = FTransform(FRotator(0, Yaw, 0), Loc, FVector(Fit(A, H)));
		FTransform Hidden(FRotator(0, Yaw, 0), Loc, FVector(0.001f));
		P.Inst = A->AddInstance(P.Xf, true);
		FTransform Bx = P.Xf; Bx.SetScale3D(FVector(0.001f));
		B->AddInstance(Bx, true);
		A->SetCustomDataValue(P.Inst, 0, 0.f); A->SetCustomDataValue(P.Inst, 1, 0.f);
		B->SetCustomDataValue(P.Inst, 0, 0.25f); B->SetCustomDataValue(P.Inst, 1, 0.f);
		Plants.Add(P);
	};
	// trees: riparian forest where the soil is irrigated, plus some on the dry highland that will not survive
	for (int32 Y = 1; Y < GH - 1; ++Y)
		for (int32 X = 1; X < GW - 1; ++X)
		{
			const int32 I = Sim.Idx(X, Y);
			if (Sim.Depth[I] > 0.01f || FarmCells.Contains(I) || Sim.Ground[I] < BaseGround[I] - 0.5f || FindStructure(I) != INDEX_NONE) continue;
			const float Rnd = Hash2(X * 7 + 3, Y * 13 + 1);
			const bool bMoist = Sim.Moist[I] > 1.f;
			const float Want = bMoist ? 0.30f : 0.035f;
			if (VNoise(X * 0.17f + 40.f, Y * 0.17f) < 0.45f || Rnd > Want) continue;
			const int32 Size = (int32)(Hash2(X, Y * 3) * 2.99f);
			const FVector Loc = CellCenter(I, Sim.Ground[I] * Block) + FVector((Hash2(X, Y + 9) - 0.5f) * 40.f, (Hash2(X + 5, Y) - 0.5f) * 40.f, 0);
			AddPlant(I, 0, Size, Loc, Hash2(Y, X) * 360.f, TreeH[Size] * (0.85f + 0.3f * Hash2(X + 1, Y + 2)));
		}
	// crops: a pumpkin per farm cell, with a gap every few rows for paths
	for (int32 I : FarmCells)
	{
		const int32 X = I % GW, Y = I / GW;
		if (Y % 4 == 0 || Sim.Moist[I] < 1.5f) continue;		// beavers only plant on irrigated soil
		AddPlant(I, 1, 0, CellCenter(I, Sim.Ground[I] * Block), Hash2(X, Y) * 360.f, 46.f + 10.f * Hash2(Y, X));
	}
	for (int32 K = 0; K < 3; ++K) { TreeISM[K]->MarkRenderStateDirty(); DeadTreeISM[K]->MarkRenderStateDirty(); }
}

void AGWWorld::SpawnDecor()
{
	const TCHAR* Plain = TEXT("/Game/GridWater/MI_Prop.MI_Prop");
	UInstancedStaticMeshComponent* Fence = MakeISM(TEXT("/Game/GridWater/Props/fence.fence"), Plain);
	UInstancedStaticMeshComponent* Lantern = MakeISM(TEXT("/Game/GridWater/Props/post_lantern.post_lantern"), Plain);
	UInstancedStaticMeshComponent* Arch = MakeISM(TEXT("/Game/GridWater/Props/arch.arch"), Plain);
	auto Fit = [](UInstancedStaticMeshComponent* C, float Want, int32 Axis) -> float {
		if (!C || !C->GetStaticMesh()) return 1.f;
		const FVector E = C->GetStaticMesh()->GetBounds().BoxExtent * 2.f;
		return Want / (float)FMath::Max(Axis == 0 ? FMath::Max(E.X, E.Y) : E.Z, 1.0);
	};
	// fences around both farms
	auto FenceRect = [&](int32 X0, int32 Y0, int32 X1, int32 Y1)
	{
		const float S = Fit(Fence, Cell, 0);
		for (int32 X = X0; X <= X1; ++X)
			for (int32 Y : { Y0, Y1 + 1 })
			{
				const int32 I = Sim.Idx(X, FMath::Min(Y, GH - 1));
				Fence->AddInstance(FTransform(FRotator(0, 0, 0), FVector((X + 0.5f) * Cell, Y * Cell, Sim.Ground[I] * Block), FVector(S)), true);
			}
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X : { X0, X1 + 1 })
			{
				if (Y == (Y0 + Y1) / 2) continue;		// gate gap
				const int32 I = Sim.Idx(FMath::Min(X, GW - 1), Y);
				Fence->AddInstance(FTransform(FRotator(0, 90, 0), FVector(X * Cell, (Y + 0.5f) * Cell, Sim.Ground[I] * Block), FVector(S)), true);
			}
		const float Ls = Fit(Lantern, 180.f, 1);
		for (const FIntPoint P : { FIntPoint(X0, Y0), FIntPoint(X1 + 1, Y0), FIntPoint(X0, Y1 + 1), FIntPoint(X1 + 1, Y1 + 1) })
		{
			const int32 I = Sim.Idx(FMath::Clamp(P.X, 0, GW - 1), FMath::Clamp(P.Y, 0, GH - 1));
			Lantern->AddInstance(FTransform(FRotator::ZeroRotator, FVector(P.X * Cell, P.Y * Cell, Sim.Ground[I] * Block), FVector(Ls)), true);
		}
	};
	FenceRect(83, 9, 92, 31);
	FenceRect(33, 51, 40, 59);
	// a stone path from the east farm gate to the reservoir pump, with benches and lanterns along it
	UInstancedStaticMeshComponent* Path = MakeISM(TEXT("/Game/GridWater/Props/path_A.path_A"), Plain);
	UInstancedStaticMeshComponent* Bench = MakeISM(TEXT("/Game/GridWater/Props/bench.bench"), Plain);
	const float Ps = Fit(Path, Cell * 0.92f, 0), Bs = Fit(Bench, 110.f, 0);
	const int32 GateY = (9 + 31) / 2;
	for (int32 X = 76; X <= 83; ++X)
	{
		const int32 I = Sim.Idx(X, GateY);
		if (Sim.Depth[I] > 0.01f || ReservoirCells.Contains(I)) continue;
		Path->AddInstance(FTransform(FRotator(0, 90 * (X % 2), 0), CellCenter(I, Sim.Ground[I] * Block + 1.f), FVector(Ps)), true);
	}
	for (int32 Y = GateY + 2; Y <= GateY + 8; Y += 6)
	{
		const int32 I = Sim.Idx(81, Y);
		if (Sim.Depth[I] <= 0.01f) Bench->AddInstance(FTransform(FRotator(0, 90, 0), CellCenter(I, Sim.Ground[I] * Block), FVector(Bs)), true);
	}
	// a stone arch over the spring
	const int32 Sp = Sim.Idx(13, 3);
	Arch->AddInstance(FTransform(FRotator(0, 0, 0), FVector(13.5f * Cell, 3.0f * Cell, Sim.Ground[Sp] * Block), FVector(Fit(Arch, 380.f, 0))), true);
}

void AGWWorld::UpdatePlants(float SimDt)
{
	if (!CropISM) return;
	for (FGWPlant& P : Plants)
	{
		if (P.bDead) continue;
		const float M = Sim.Moist[P.Cell], Sc = Sim.SoilC[P.Cell];
		if (Sc > 0.4f) P.Health -= SimDt / (0.3f * DayLen);						// contaminated soil kills within ~0.3 day
		else if (M <= 0.01f) P.Health -= SimDt / ((P.Kind == 1 ? 1.2f : 2.5f) * DayLen);	// dry soil: crops wither in ~1 day
		else P.Health = FMath::Min(1.f, P.Health + SimDt / (0.5f * DayLen));
		UInstancedStaticMeshComponent* A = P.Kind == 0 ? TreeISM[P.Size].Get() : CropISM.Get();
		UInstancedStaticMeshComponent* B = P.Kind == 0 ? DeadTreeISM[P.Size].Get() : DeadCropISM.Get();
		if (P.Health <= 0.f)
		{
			P.bDead = true; P.Health = 0.f;
			FTransform Z = P.Xf; Z.SetScale3D(FVector(0.001f));
			A->UpdateInstanceTransform(P.Inst, Z, true, false);
			FTransform D = P.Xf;
			if (P.Kind == 1) D.SetScale3D(P.Xf.GetScale3D() * 1.0f);
			B->UpdateInstanceTransform(P.Inst, D, true, false);
			B->SetCustomDataValue(P.Inst, 1, Sc, false);
		}
		else
		{
			A->SetCustomDataValue(P.Inst, 0, 1.f - P.Health, false);
			A->SetCustomDataValue(P.Inst, 1, Sc, false);
		}
	}
	for (int32 K = 0; K < TreeISM.Num(); ++K) { TreeISM[K]->MarkRenderStateDirty(); DeadTreeISM[K]->MarkRenderStateDirty(); }
	CropISM->MarkRenderStateDirty(); DeadCropISM->MarkRenderStateDirty();
}

// =================================================================================================
// helpers
// =================================================================================================
FVector AGWWorld::CellCenter(int32 CellIdx, float Z) const
{
	return FVector((CellIdx % GW + 0.5f) * Cell, (CellIdx / GW + 0.5f) * Cell, Z);
}

bool AGWWorld::TraceCell(const FVector& Origin, const FVector& Dir, int32& OutCell, FVector& OutHit, int32* OutFaceCell) const
{
	const FVector D = Dir.GetSafeNormal();
	int32 Last = INDEX_NONE;
	for (float T = 0.f; T < 30000.f; T += 8.f)
	{
		const FVector P = Origin + D * T;
		const int32 X = FMath::FloorToInt(P.X / Cell), Y = FMath::FloorToInt(P.Y / Cell);
		if (!Sim.In(X, Y)) { if (P.Z < -300.f) return false; continue; }
		const int32 I = Sim.Idx(X, Y);
		if (P.Z <= SurfaceZ(I) + 1.f)
		{
			OutCell = I; OutHit = P;
			if (OutFaceCell) *OutFaceCell = (P.Z < SurfaceZ(I) - 12.f && Last != INDEX_NONE) ? Last : I;
			return true;
		}
		Last = I;
	}
	return false;
}

void AGWWorld::SetOverlay(EGWOverlay O)
{
	Overlay = O;
	for (UMaterialInstanceDynamic* M : { TerrainMID.Get(), WaterMID.Get() })
		if (M) M->SetScalarParameterValue(TEXT("Overlay"), (float)(uint8)O);
}

float AGWWorld::ReservoirVolume() const
{
	float V = 0.f; for (int32 I : ReservoirCells) V += Sim.Depth[I]; return V;
}

float AGWWorld::ReservoirContamination() const
{
	float V = 0.f, M = 0.f;
	for (int32 I : ReservoirCells) { V += Sim.Depth[I]; M += Sim.Depth[I] * Sim.Conc[I]; }
	return V > 1e-3f ? M / V : 0.f;
}

float AGWWorld::CropSurvival() const
{
	int32 Total = 0, Alive = 0;
	for (const FGWPlant& P : Plants) if (P.Kind == 1) { ++Total; Alive += P.bDead ? 0 : 1; }
	return Total ? (float)Alive / Total : 1.f;
}

double AGWWorld::PumpedTotal() const { double T = 0; for (const FGWStructure& S : Structures) T += S.Pumped; return T; }
double AGWWorld::PumpedBadTotal() const { double T = 0; for (const FGWStructure& S : Structures) T += S.PumpedBad; return T; }

void AGWWorld::ResetWaterOnly()
{
	Sim.ClearWater();
	for (FGWStructure& S : Structures) { S.Pumped = S.PumpedBad = 0; }
}

void AGWWorld::DrawFlowArrows() const
{
	UWorld* W = GetWorld();
	for (int32 Y = 1; Y < GH; Y += 2)
		for (int32 X = 1; X < GW; X += 2)
		{
			const int32 I = Sim.Idx(X, Y);
			if (Sim.Depth[I] < 0.03f) continue;
			const FVector2f V(Sim.VelX[I], Sim.VelY[I]);
			const float Sp = V.Size();
			if (Sp < 0.05f) continue;
			const FVector2f Dn = V / Sp;
			const float Len = FMath::Clamp(Sp * 60.f, 25.f, 150.f);
			const FVector C = CellCenter(I, SurfaceZ(I) + 6.f);
			const FVector A = C - FVector(Dn.X, Dn.Y, 0) * Len * 0.5f, B = C + FVector(Dn.X, Dn.Y, 0) * Len * 0.5f;
			const FColor Col = FLinearColor::LerpUsingHSV(FLinearColor(0.2f, 0.9f, 1.f), FLinearColor(1.f, 0.35f, 0.1f), FMath::Clamp(Sp / 2.5f, 0.f, 1.f)).ToFColor(true);
			DrawDebugDirectionalArrow(W, A, B, 30.f, Col, false, -1.f, 0, 4.f);
		}
}
