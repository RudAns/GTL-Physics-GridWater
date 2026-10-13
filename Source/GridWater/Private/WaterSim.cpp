#include "WaterSim.h"

#include "Async/ParallelFor.h"

namespace
{
	float SmoothStep01(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / FMath::Max(B - A, 1e-6f), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
}

void FWaterSim::Init(int32 InW, int32 InH)
{
	W = InW; H = InH; N = W * H;
	auto Zero = [this](auto& A, auto V) { A.Init(V, N); };
	Zero(Ground, 0.f); Zero(Solid, 0.f); Zero(InflowLimit, -1.f); Zero(DirLimit, (int8)-1); Zero(Blocked, (uint8)0); Zero(Drain, (uint8)0);
	Zero(Depth, 0.f); Zero(Conc, 0.f); Zero(VelX, 0.f); Zero(VelY, 0.f);
	Zero(WetN, (uint8)0); Zero(Sat, (uint8)0); Zero(EvapMod, 1.f); Zero(Moist, 0.f); Zero(SoilRange, 0.f); Zero(SoilC, 0.f);
	Zero(NewDepth, 0.f); Zero(NewConc, 0.f); Zero(Prev, 0.f);
	Flow.Init(0.f, N * 4); Eff.Init(0.f, N * 4);
	RowDrain.Init(0, H); RowEvap.Init(0, H); RowCDrain.Init(0, H); RowCEvap.Init(0, H); RowCreated.Init(0, H);
	L.Reset();
}

float FWaterSim::EvapRate(int32 I, float D) const
{
	const float Base = D < S.FastEvaporationDepth ? S.FastEvaporation : S.NormalEvaporation;
	return Base * (S.bSaturationEvaporation ? EvapMod[I] : 1.f) * S.EvaporationMultiplier;
}

float FWaterSim::OutflowFor(int32 I, int32 K, float Hi, float D, float Dt) const
{
	const int32 X = I % W, Y = I / W, NX = X + DX[K], NY = Y + DY[K];
	const bool bOut = !In(NX, NY);
	float Hj, FloorJ, Lim = -1.f;
	bool bTargetDry = false;
	if (bOut)
	{
		if (!Drain[I]) return 0.f;
		Hj = FloorJ = Ground[I] - 2.f;			// the map edge acts as a cliff
	}
	else
	{
		const int32 J = Idx(NX, NY);
		if (Blocked[I] || Blocked[J]) return 0.f;
		if (DirLimit[J] >= 0 && DirLimit[J] != K) return 0.f;	// a valve only accepts water along its direction
		if (DirLimit[I] >= 0 && DirLimit[I] != K) return 0.f;	// ...and only releases it the same way
		Hj = Head(J); FloorJ = Floor(J); Lim = InflowLimit[J]; bTargetDry = Depth[J] <= 0.f;
	}
	const float Prev0 = Eff[I * 4 + K];		// the game carries the balanced flow from the previous substep
	const float Damp = S.Damping == EGWDamping::Game0999 ? 0.999f : S.Damping == EGWDamping::Strong ? 0.95f : 1.f;
	float Base = Prev0 * Damp;
	float Dh = Hi - Hj;
	float F;
	if (Lim >= 0.f)
	{
		const float Above = Hi - FloorJ;
		if (!S.bSoftDam)
		{
			if (Above < Lim) return 0.f;			// hard cut: the flow snaps to zero below the crest
			F = Base + S.FlowFactor * Dt * Dh;
		}
		else if (Above < Lim)
		{
			const float HardStart = Lim - S.HardDamOffset;
			const float T = 1.f - FMath::Clamp((Above - HardStart) / FMath::Max(S.HardDamOffset, 1e-4f), 0.f, 1.f);
			const float Dec = Above < HardStart ? S.MaxHardDamDecrease : FMath::Lerp(0.f, S.MaxHardDamDecrease, T);
			F = Base - Dec;						// below the crest the flow decays gradually instead of snapping
		}
		else
		{
			float Inc = S.FlowFactor * Dt * Dh;
			if (Above < Lim + S.SoftDamOffset && Inc > 0.f) Inc *= (Above - Lim) / S.SoftDamOffset;
			F = Base + Inc;
		}
	}
	else
	{
		if (S.bSpillThreshold && !bOut && bTargetDry && FMath::Abs(Floor(I) - FloorJ) < 0.01f) Dh -= S.SpillThreshold;
		F = Base + S.FlowFactor * Dt * Dh;
	}
	if (S.Damping == EGWDamping::SemiImplicit)
		F = F / (1.f + Dt * S.Friction * FMath::Abs(Prev0) / FMath::Max(D, 0.05f));
	return FMath::Max(F, 0.f);
}

void FWaterSim::Substep(float Dt)
{
	// ---- 1. outflows: each cell only writes its own four pipes ----
	ParallelFor(H, [&](int32 Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			float* F = &Flow[I * 4];
			const float D = Depth[I];
			if (D <= 0.f) { F[0] = F[1] = F[2] = F[3] = 0.f; continue; }
			const float Hi = Head(I);
			float Sum = 0.f;
			for (int32 K = 0; K < 4; ++K) { F[K] = OutflowFor(I, K, Hi, D, Dt); Sum += F[K]; }
			if (S.bKScaling && Sum * Dt > D)
			{
				const float Kf = D / (Sum * Dt);
				for (int32 K = 0; K < 4; ++K) F[K] *= Kf;
			}
		}
	});

	// ---- 2. depth, effective flows, velocity, evaporation (gather) ----
	ParallelFor(H, [&](int32 Y)
	{
		double Drained = 0, Evap = 0, CDrained = 0, CEvap = 0, Created = 0;
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			const float* F = &Flow[I * 4];
			float InSum = 0.f, OutSum = 0.f;
			float FIn[4] = { 0, 0, 0, 0 };
			for (int32 K = 0; K < 4; ++K)
			{
				OutSum += F[K];
				const int32 NX = X + DX[K], NY = Y + DY[K];
				if (In(NX, NY))
				{
					FIn[K] = Flow[Idx(NX, NY) * 4 + Opp(K)];
					InSum += FIn[K];
					Eff[I * 4 + K] = FMath::Max(0.f, F[K] - FIn[K] * S.OutflowBalancingScaler);
				}
				else
				{
					Eff[I * 4 + K] = F[K];
					Drained += F[K] * Dt; CDrained += F[K] * Dt * Conc[I];
				}
			}
			const float D = Depth[I];
			float Nd = D + Dt * (InSum - OutSum);
			if (Nd > 0.f)
			{
				const float Take = FMath::Min(Nd, EvapRate(I, D) * Dt);
				Nd -= Take; Evap += Take; CEvap += Take * Conc[I];
				Prev[I] = Take;
			}
			else Prev[I] = 0.f;
			if (Nd < 0.f) { Created += -Nd; Nd = 0.f; }
			NewDepth[I] = Nd;
			// Mei 2007 velocity estimate from the pipe fluxes through the cell
			const float Avg = FMath::Max(0.5f * (D + Nd), 0.08f);
			VelX[I] = 0.5f * (F[0] - FIn[0] + FIn[2] - F[2]) / Avg;
			VelY[I] = 0.5f * (F[1] - FIn[1] + FIn[3] - F[3]) / Avg;
		}
		RowDrain[Y] = Drained; RowEvap[Y] = Evap; RowCDrain[Y] = CDrained; RowCEvap[Y] = CEvap; RowCreated[Y] = Created;
	});

	// ---- 3. contamination advection ----
	ParallelFor(H, [&](int32 Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			const float Nd = NewDepth[I];
			if (Nd <= 1e-5f) { NewConc[I] = 0.f; continue; }
			float C = Conc[I];
			if (S.Contam == EGWContam::GameEffective)
			{
				// game: the column keeps its old water at its own concentration and mixes in the effective inflows
				float Recv = 0.f, Change = 0.f;
				for (int32 K = 0; K < 4; ++K)
				{
					const int32 NX = X + DX[K], NY = Y + DY[K];
					if (!In(NX, NY)) continue;
					const int32 J = Idx(NX, NY);
					const float E = Eff[J * 4 + Opp(K)] * Dt;
					if (E > 0.f) { Recv += E; Change += E * Conc[J]; }
				}
				if (Recv > 0.f) C = (Conc[I] * (Nd - Recv) + Change) / Nd;
			}
			else
			{
				// proposal: carry contaminant mass with exactly the same fluxes as the water
				float M = Conc[I] * Depth[I] - Prev[I] * Conc[I];
				const float* F = &Flow[I * 4];
				for (int32 K = 0; K < 4; ++K)
				{
					M -= F[K] * Dt * Conc[I];
					const int32 NX = X + DX[K], NY = Y + DY[K];
					if (!In(NX, NY)) continue;
					const int32 J = Idx(NX, NY);
					M += Flow[J * 4 + Opp(K)] * Dt * Conc[J];
				}
				C = M / Nd;
			}
			NewConc[I] = FMath::Clamp(C, 0.f, 1.f);
		}
	});

	// ---- 4. gated diffusion (symmetric pairwise exchange, so it conserves contaminant mass) ----
	if (S.Diffusion != EGWDiffusion::Off)
	{
		ParallelFor(H, [&](int32 Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 I = Idx(X, Y);
				const float Di = NewDepth[I];
				Prev[I] = 0.f;
				if (Di <= 1e-4f || Blocked[I]) continue;
				const float Hi = Floor(I) + Di;
				float Sum = 0.f;
				for (int32 K = 0; K < 4; ++K)
				{
					const int32 NX = X + DX[K], NY = Y + DY[K];
					if (!In(NX, NY)) continue;
					const int32 J = Idx(NX, NY);
					const float Dj = NewDepth[J];
					if (Dj <= 1e-4f || Blocked[J]) continue;
					const float Net = FMath::Abs(Flow[I * 4 + K] - Flow[J * 4 + Opp(K)]);
					const float Dh = FMath::Abs(Hi - (Floor(J) + Dj));
					const bool bDamShallow = (Di <= 1.f && InflowLimit[I] >= 0.f) || (Dj <= 1.f && InflowLimit[J] >= 0.f);
					float Wt = 1.f;
					if (S.Diffusion == EGWDiffusion::BoolGate)
						Wt = (Net < S.DiffusionOutflowLimit && Dh <= S.DiffusionDepthLimit && !bDamShallow) ? 1.f : 0.f;
					else if (S.Diffusion == EGWDiffusion::SmoothGate)
						Wt = bDamShallow ? 0.f : (1.f - SmoothStep01(0.5f * S.DiffusionOutflowLimit, S.DiffusionOutflowLimit, Net)) *
							(1.f - SmoothStep01(0.5f * S.DiffusionDepthLimit, S.DiffusionDepthLimit, Dh));
					if (Wt <= 0.f) continue;
					Sum += S.DiffusionRate * Dt * 0.5f * Wt * (NewConc[J] - NewConc[I]) * FMath::Min(Di, Dj);
				}
				Prev[I] = Sum / Di;
			}
		});
		for (int32 I = 0; I < N; ++I) NewConc[I] = FMath::Clamp(NewConc[I] + Prev[I], 0.f, 1.f);
	}

	Swap(Depth, NewDepth);
	Swap(Conc, NewConc);
	CreatedWater = 0.f;
	for (int32 Y = 0; Y < H; ++Y)
	{
		L.Drained += RowDrain[Y]; L.Evaporated += RowEvap[Y]; L.CDrained += RowCDrain[Y]; L.CEvaporated += RowCEvap[Y];
		CreatedWater += (float)RowCreated[Y];
	}
	CreatedTotal += CreatedWater;
}

void FWaterSim::UpdateDerived(float TickDt)
{
	// watered neighbours (8-neighbourhood, +1 for the cell itself)
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			if (Depth[I] <= 0.001f) { WetN[I] = 0; continue; }
			int32 C = 1;
			for (int32 Oy = -1; Oy <= 1; ++Oy)
				for (int32 Ox = -1; Ox <= 1; ++Ox)
					if ((Ox || Oy) && In(X + Ox, Y + Oy) && Depth[Idx(X + Ox, Y + Oy)] > 0.001f) ++C;
			WetN[I] = (uint8)C;
		}
	// cluster saturation: own count, or a 4-neighbour's count minus one, capped at 8 (MaxClusterSaturation)
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			if (!WetN[I]) { Sat[I] = 0; EvapMod[I] = 1.f; continue; }
			int32 V = WetN[I];
			for (int32 K = 0; K < 4; ++K)
				if (In(X + DX[K], Y + DY[K])) V = FMath::Max(V, (int32)WetN[Idx(X + DX[K], Y + DY[K])] - 1);
			V = FMath::Min(V, 8);
			Sat[I] = (uint8)V;
			const float Nn = 10.f - V;
			EvapMod[I] = 0.0595f * Nn * Nn + 0.101f * Nn + 0.72f;	// x6.45 for a lone puddle, x1.16 inside a lake
		}

	// soil moisture (0..16) and soil contamination reach (0..7)
	TArray<float> LastM = Moist, LastR = SoilRange;
	const float Decay = 1.25f * TickDt, Spread = 6.66f * TickDt;
	auto WaterMoisture = [&](int32 J) -> float
	{
		const float C = Conc[J];
		const float R = 2.f * Sat[J];
		if (C < 0.01f) return R;
		const float F = C / 0.53f;
		return F >= 1.f ? 0.f : R * (1.f - F);
	};
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			const float G = Ground[I];
			float FromWater = 0.f, FromNb = 0.f, BadSrc = 0.f, FromNbR = 0.f;
			const bool bWet = Depth[I] > 0.001f;
			if (bWet) { FromWater = WaterMoisture(I); if (Conc[I] > 0.5f) BadSrc = 7.f; }
			for (int32 K = 0; K < 4; ++K)
			{
				const int32 NX = X + DX[K], NY = Y + DY[K];
				if (!In(NX, NY)) continue;
				const int32 J = Idx(NX, NY);
				if (Depth[J] <= 0.001f) continue;
				const float Surf = FMath::CeilToFloat(Floor(J) + Depth[J]);
				const float Up = FMath::Max(0.f, G - Surf);
				FromWater = FMath::Max(FromWater, WaterMoisture(J) - Up * 6.f);
				if (Conc[J] > 0.5f) BadSrc = FMath::Max(BadSrc, 7.f - Up * 5.f);
			}
			for (int32 Oy = -1; Oy <= 1; ++Oy)
				for (int32 Ox = -1; Ox <= 1; ++Ox)
				{
					if (!(Ox || Oy) || !In(X + Ox, Y + Oy)) continue;
					const int32 J = Idx(X + Ox, Y + Oy);
					const float Cost = (Ox && Oy) ? 1.414f : 1.f;
					const float Climb = FMath::Max(0.f, G - Ground[J] - FMath::CeilToFloat(Depth[J]));
					if (LastM[J] > 0.f) FromNb = FMath::Max(FromNb, LastM[J] - Climb * 6.f - Cost);
					if (LastR[J] > 0.f) FromNbR = FMath::Max(FromNbR, LastR[J] - Climb * 5.f - Cost);
				}
			// moisture approaches the water-derived value at the spreading rate and decays otherwise
			const float Decayed = FMath::Max(0.f, LastM[I] - Decay);
			float M = Decayed;
			if (FromWater > Decayed && FromWater >= FromNb) M = FMath::Min(FromWater, LastM[I] + Spread);
			else if (FromNb > Decayed) M = FromNb;
			if (bWet) M *= (1.f - Conc[I]);
			Moist[I] = M < 0.01f ? 0.f : FMath::Min(M, 16.f);
			SoilRange[I] = FMath::Max(BadSrc, FromNbR);
			SoilC[I] = FMath::Clamp(SoilC[I] + (SoilRange[I] > 0.f ? 0.066f : -0.033f) * TickDt, 0.f, 1.f);
		}
}

void FWaterSim::AddWater(int32 I, float Amount, float C)
{
	if (Amount <= 0.f) return;
	const float D = Depth[I];
	Conc[I] = FMath::Clamp((Conc[I] * D + C * Amount) / (D + Amount), 0.f, 1.f);
	Depth[I] = D + Amount;
	L.Injected += Amount; L.CInjected += Amount * C;
}

float FWaterSim::RemoveWater(int32 I, float Amount, float* OutContam)
{
	const float Take = FMath::Clamp(Amount, 0.f, Depth[I]);
	Depth[I] -= Take;
	L.Pumped += Take; L.CPumped += Take * Conc[I];
	if (OutContam) *OutContam = Take * Conc[I];
	if (Depth[I] <= 0.f) Conc[I] = 0.f;
	return Take;
}

void FWaterSim::ClearWater()
{
	for (int32 I = 0; I < N; ++I) { Depth[I] = 0.f; Conc[I] = 0.f; VelX[I] = VelY[I] = 0.f; }
	for (float& F : Flow) F = 0.f;
	for (float& F : Eff) F = 0.f;
	ResetLedger();
}

double FWaterSim::TotalWater() const
{
	double T = 0; for (float D : Depth) T += D; return T;
}

double FWaterSim::TotalContaminant() const
{
	double T = 0; for (int32 I = 0; I < N; ++I) T += (double)Depth[I] * Conc[I]; return T;
}

void FWaterSim::ResetLedger()
{
	L.Reset();
	L.Initial = TotalWater();
	L.CInitial = TotalContaminant();
	CreatedTotal = 0;
}
