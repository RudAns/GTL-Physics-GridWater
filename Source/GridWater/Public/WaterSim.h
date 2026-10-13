#pragma once

#include "CoreMinimal.h"

/**
 * Timberborn-style grid water (report ch.02-05), single layer (2.5D):
 * virtual pipes with 0.999 damping, outflow scaling (K), soft/hard dams, spill threshold, one-way valves,
 * a contamination scalar with upwind advection and gated diffusion, depth-banded evaporation scaled by
 * cluster saturation, and the soil moisture / soil contamination fields derived from the water.
 * Every "principle" the report discusses is a switch in FGWSimSettings so the demo can compare them.
 */

enum class EGWDamping : uint8 { Game0999, Strong, SemiImplicit };
enum class EGWContam : uint8 { GameEffective, MassConserving };
enum class EGWDiffusion : uint8 { BoolGate, SmoothGate, NoGate, Off };

struct FGWSimSettings
{
	bool bKScaling = true;
	EGWDamping Damping = EGWDamping::Game0999;
	bool bSoftDam = true;
	bool bSpillThreshold = true;
	EGWContam Contam = EGWContam::GameEffective;
	EGWDiffusion Diffusion = EGWDiffusion::BoolGate;
	bool bSaturationEvaporation = true;

	// constants from the Update 6 era blueprints (report ch.03-05)
	float FlowFactor = 2.25f;
	float SpillThreshold = 0.1f;
	float SoftDamOffset = 0.1f;
	float HardDamOffset = 0.05f;
	float MaxHardDamDecrease = 0.02f;
	float OutflowBalancingScaler = 0.8f;
	float NormalEvaporation = 0.0001f;
	float FastEvaporation = 0.001f;
	float FastEvaporationDepth = 0.02f;
	float DiffusionOutflowLimit = 0.125f;
	float DiffusionDepthLimit = 0.1f;
	float DiffusionRate = 0.45f;
	float Friction = 0.6f;			// semi-implicit friction coefficient (report ch.08 proposal)

	// demo compression: one demo day is 60 simulated seconds, so evaporation is sped up
	float EvaporationMultiplier = 25.f;
};

struct FGWLedger
{
	double Injected = 0, Drained = 0, Evaporated = 0, Pumped = 0, Initial = 0;
	double CInjected = 0, CDrained = 0, CEvaporated = 0, CPumped = 0, CInitial = 0;
	void Reset() { *this = FGWLedger(); }
};

class GRIDWATER_API FWaterSim
{
public:
	static constexpr int32 DX[4] = { 1, 0, -1, 0 };
	static constexpr int32 DY[4] = { 0, 1, 0, -1 };
	static int32 Opp(int32 K) { return (K + 2) & 3; }

	int32 W = 0, H = 0, N = 0;
	FGWSimSettings S;
	FGWLedger L;

	// static-ish map data
	TArray<float> Ground;		// terrain top, in blocks (integer)
	TArray<float> Solid;		// extra solid height from structures (levee)
	TArray<float> InflowLimit;	// < 0: none, else water must rise this far above the cell floor to enter (dam, floodgate)
	TArray<int8> DirLimit;		// < 0: none, else the only direction water may move through the cell (valve)
	TArray<uint8> Blocked;		// closed valve: no flow in or out
	TArray<uint8> Drain;		// map-edge cells that spill off the map

	// water state
	TArray<float> Depth;
	TArray<float> Conc;			// contamination 0..1
	TArray<float> Flow;			// N*4 outflows (cells^3 / s)
	TArray<float> Eff;			// N*4 "effective" outflows (outflow - 0.8 * counter-flow), used by the game's advection
	TArray<float> VelX, VelY;	// cell velocity estimate (cells / s)

	// derived fields
	TArray<uint8> WetN, Sat;
	TArray<float> EvapMod;
	TArray<float> Moist;		// 0..16 (tiles of irrigation reach)
	TArray<float> SoilRange;	// soil contamination reach (0..7)
	TArray<float> SoilC;		// 0..1

	// stats of the last substep
	float CreatedWater = 0.f;	// water created by clamping negative depths (only when K scaling is off)
	double CreatedTotal = 0;

	void Init(int32 InW, int32 InH);
	int32 Idx(int32 X, int32 Y) const { return Y * W + X; }
	bool In(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < W && Y < H; }
	float Floor(int32 I) const { return Ground[I] + Solid[I]; }
	float Head(int32 I) const { return Ground[I] + Solid[I] + Depth[I]; }

	/** One substep (the game runs two per 0.6 s tick). */
	void Substep(float Dt);
	/** Saturation, evaporation modifiers, moisture and soil contamination (once per tick). */
	void UpdateDerived(float TickDt);

	void AddWater(int32 I, float Amount, float C);
	float RemoveWater(int32 I, float Amount, float* OutContam = nullptr);
	void ClearWater();

	double TotalWater() const;
	double TotalContaminant() const;
	double WaterError() const { return TotalWater() - (L.Initial + L.Injected - L.Drained - L.Evaporated - L.Pumped); }
	double ContamError() const { return TotalContaminant() - (L.CInitial + L.CInjected - L.CDrained - L.CEvaporated - L.CPumped); }
	void ResetLedger();

private:
	TArray<float> NewDepth, NewConc, Prev;
	TArray<double> RowDrain, RowEvap, RowCDrain, RowCEvap, RowCreated;
	float OutflowFor(int32 I, int32 K, float Hi, float D, float Dt) const;
	float EvapRate(int32 I, float D) const;
};
