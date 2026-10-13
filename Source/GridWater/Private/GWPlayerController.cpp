#include "GWPlayerController.h"

#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GWCamera.h"
#include "GWGameMode.h"
#include "GWHud.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

AGWPlayerController::AGWPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	DefaultMouseCursor = EMouseCursor::Default;
}

AGWCamera* AGWPlayerController::GetCam() const { return Cast<AGWCamera>(GetPawn()); }

void AGWPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (AGWGameMode* GM = GetWorld()->GetAuthGameMode<AGWGameMode>()) Water = GM->WaterWorld;
	if (!Water) for (TActorIterator<AGWWorld> It(GetWorld()); It; ++It) Water = *It;

	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);

	if (AGWCamera* C = GetCam())
	{
		C->Focus(FVector(4800, 3500, 450), 12500.f, -90.f);
		C->SnapNow();
	}
	if (Water)
	{
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Highlight = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, P);
		if (Highlight)
		{
			Highlight->SetMobility(EComponentMobility::Movable);
			Highlight->GetStaticMeshComponent()->SetStaticMesh(Water->MeshCube());
			Highlight->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Highlight->GetStaticMeshComponent()->SetCastShadow(false);
			if (Water->GhostMID) Highlight->GetStaticMeshComponent()->SetMaterial(0, Water->GhostMID);
			Highlight->SetActorHiddenInGame(true);
		}
		Water->SpeedIndex = 0;		// paused behind the intro card
	}
	Hud = CreateWidget<UGWHud>(this, UGWHud::StaticClass());
	if (Hud) Hud->AddToViewport();

	FParse::Value(FCommandLine::Get(), TEXT("GWCapture="), CaptureDir);
	if (!CaptureDir.IsEmpty()) CaptureStep = 0;
}

// =================================================================================================
// names & help text
// =================================================================================================
FString AGWPlayerController::ToolName(EGWTool T)
{
	switch (T)
	{
	case EGWTool::Levee: return TEXT("堤坝");
	case EGWTool::Dam: return TEXT("大坝");
	case EGWTool::Floodgate: return TEXT("防洪闸");
	case EGWTool::Valve: return TEXT("自动阀");
	case EGWTool::Pump: return TEXT("水泵");
	case EGWTool::Dig: return TEXT("挖掘");
	case EGWTool::Raise: return TEXT("堆土");
	case EGWTool::Erase: return TEXT("拆除");
	default: return TEXT("查看");
	}
}

FString AGWPlayerController::ToolKey(EGWTool T)
{
	switch (T)
	{
	case EGWTool::Levee: return TEXT("1");
	case EGWTool::Dam: return TEXT("2");
	case EGWTool::Floodgate: return TEXT("3");
	case EGWTool::Valve: return TEXT("4");
	case EGWTool::Pump: return TEXT("5");
	case EGWTool::Dig: return TEXT("6");
	case EGWTool::Raise: return TEXT("7");
	case EGWTool::Erase: return TEXT("X");
	default: return TEXT("Esc");
	}
}

FString AGWPlayerController::ToolHelp(EGWTool T)
{
	switch (T)
	{
	case EGWTool::Levee: return TEXT("两格高的实体，完全挡水。模拟原语：实体障碍");
	case EGWTool::Dam: return TEXT("半格高的低坝，水位超过坝顶才放行。模拟原语：流入限制（软硬坝）");
	case EGWTool::Floodgate: return TEXT("闸板高度可调：整数部分是实体，小数部分是流入限制。悬停后按 [ ] 调高度");
	case EGWTool::Valve: return TEXT("单向阀：上游污染度超过阈值自动关闭。点击已建好的阀切换 自动/常开/常闭");
	case EGWTool::Pump: return TEXT("从相邻水格抽水，清水与恶水按比例计入。建在水边");
	case EGWTool::Dig: return TEXT("地面降低一格，可拖动连续挖渠");
	case EGWTool::Raise: return TEXT("地面抬高一格，可拖动连续堆土");
	case EGWTool::Erase: return TEXT("拆除建筑");
	default: return TEXT("悬停查看格子数据，点击自动阀切换模式");
	}
}

FString AGWPlayerController::PrincipleName(int32 I) const
{
	static const TCHAR* N[] = { TEXT("出流缩放 K"), TEXT("流量阻尼"), TEXT("坝体放行"), TEXT("漫流阈值"), TEXT("污染输运"), TEXT("污染扩散"), TEXT("蒸发模型") };
	return N[FMath::Clamp(I, 0, NumPrinciples - 1)];
}

FString AGWPlayerController::PrincipleValue(int32 I) const
{
	if (!Water) return FString();
	const FGWSimSettings& S = Water->Sim.S;
	switch (I)
	{
	case 0: return S.bKScaling ? TEXT("开（原作）") : TEXT("关");
	case 1: return S.Damping == EGWDamping::Game0999 ? TEXT("0.999（原作）") : S.Damping == EGWDamping::Strong ? TEXT("强阻尼 0.95") : TEXT("半隐式摩擦（改进）");
	case 2: return S.bSoftDam ? TEXT("软硬坝（原作）") : TEXT("硬截断");
	case 3: return S.bSpillThreshold ? TEXT("开（原作）") : TEXT("关");
	case 4: return S.Contam == EGWContam::GameEffective ? TEXT("有效出流（原作）") : TEXT("质量守恒（改进）");
	case 5: return S.Diffusion == EGWDiffusion::BoolGate ? TEXT("布尔门控（原作）") : S.Diffusion == EGWDiffusion::SmoothGate ? TEXT("平滑门控（改进）") : S.Diffusion == EGWDiffusion::NoGate ? TEXT("无门控") : TEXT("关闭");
	default: return S.bSaturationEvaporation ? TEXT("按水面大小（原作）") : TEXT("统一速率");
	}
}

bool AGWPlayerController::PrincipleIsOriginal(int32 I) const
{
	return PrincipleValue(I).Contains(TEXT("原作"));
}

void AGWPlayerController::TogglePrinciple(int32 I)
{
	if (!Water) return;
	FGWSimSettings& S = Water->Sim.S;
	FString Body;
	switch (I)
	{
	case 0: S.bKScaling = !S.bKScaling;
		Body = S.bKScaling ? TEXT("出流总量超过本格水量时，四个方向按同一比例缩小，水深不会变负，总水量严格守恒。")
			: TEXT("不再缩放：浅水格一步流出的水比它拥有的还多，负水深被截断为 0，相当于凭空造水。看左下角“守恒误差”持续增长。"); break;
	case 1: S.Damping = (EGWDamping)(((uint8)S.Damping + 1) % 3);
		Body = S.Damping == EGWDamping::Game0999 ? TEXT("原作每步只耗散 0.1% 的流量，水有惯性：关闸或决堤时水面会来回晃动。")
			: S.Damping == EGWDamping::Strong ? TEXT("每步耗散 5%：晃动消失，但水流明显变黏，长河道的流量下降。")
			: TEXT("半隐式摩擦（报告第 8 章）：浅而急的水阻尼大，深水库保留惯性，且对任意时间步稳定。"); break;
	case 2: S.bSoftDam = !S.bSoftDam;
		Body = S.bSoftDam ? TEXT("水位低于坝顶时流量逐步衰减，略高于坝顶时按比例放行，大坝处水面平稳。")
			: TEXT("硬截断：流量在坝顶处突变为 0 或全开，坝前水面容易抖动。开发者正是为了避免这种振荡才设计了软硬坝。"); break;
	case 3: S.bSpillThreshold = !S.bSpillThreshold;
		Body = S.bSpillThreshold ? TEXT("水流向同高度的干地时先扣除 0.1 格水头，薄水膜不会无限摊开。")
			: TEXT("关掉后浅水会在平地上摊成大片薄膜，按快速档蒸发，水量损失明显变大。"); break;
	case 4: S.Contam = S.Contam == EGWContam::GameEffective ? EGWContam::MassConserving : EGWContam::GameEffective;
		Body = S.Contam == EGWContam::GameEffective ? TEXT("原作用“出流 − 0.8 × 对向流”搬运污染，与水量通量不一致，污染物总量会漂移。看左下角“污染物误差”。")
			: TEXT("改进：污染物质量与水量用完全相同的通量搬运，污染物误差归零（报告第 8 章）。"); break;
	case 5: S.Diffusion = (EGWDiffusion)(((uint8)S.Diffusion + 1) % 4);
		Body = S.Diffusion == EGWDiffusion::BoolGate ? TEXT("原作只在净流量 < 0.125 且水面高差 ≤ 0.1 的界面扩散：急流以平流为主，静水缓慢混合。")
			: S.Diffusion == EGWDiffusion::SmoothGate ? TEXT("平滑门控：阈值附近连续过渡，污染前沿不会时快时慢。")
			: S.Diffusion == EGWDiffusion::NoGate ? TEXT("无门控：急流里也扩散，恶水会逆着水流爬向上游，截流变得困难。")
			: TEXT("关闭扩散：只剩平流，静止的水库里出现清浊分明的界线。"); break;
	default: S.bSaturationEvaporation = !S.bSaturationEvaporation;
		Body = S.bSaturationEvaporation ? TEXT("蒸发倍率由聚簇饱和度决定：孤立水坑约 ×6.45，大湖内部约 ×1.16。切到“聚簇饱和度”视图对照。")
			: TEXT("统一速率：小水坑不再更快干涸，一格水也能长期维持灌溉。"); break;
	}
	Toast(PrincipleName(I) + TEXT("：") + PrincipleValue(I), Body);
}

FString AGWPlayerController::ParamName(int32 I) const
{
	static const TCHAR* N[] = { TEXT("水源强度"), TEXT("蒸发倍率"), TEXT("扩散率"), TEXT("自动阀阈值"), TEXT("新防洪闸高度") };
	return N[FMath::Clamp(I, 0, NumParams - 1)];
}

FString AGWPlayerController::ParamValue(int32 I) const
{
	if (!Water) return FString();
	switch (I)
	{
	case 0: return FString::Printf(TEXT("%.1f 格³/秒"), Water->SourceStrength);
	case 1: return FString::Printf(TEXT("×%.0f"), Water->Sim.S.EvaporationMultiplier);
	case 2: return FString::Printf(TEXT("%.2f"), Water->Sim.S.DiffusionRate);
	case 3: return FString::Printf(TEXT("%.0f%%"), Water->ValveThreshold * 100.f);
	default: return FString::Printf(TEXT("%.2f 格"), Water->DefaultGateH);
	}
}

void AGWPlayerController::AdjustParam(int32 I, int32 Delta)
{
	if (!Water) return;
	switch (I)
	{
	case 0: Water->SourceStrength = FMath::Clamp(Water->SourceStrength + Delta * 0.5f, 0.5f, 10.f); break;
	case 1: Water->Sim.S.EvaporationMultiplier = FMath::Clamp(Water->Sim.S.EvaporationMultiplier + Delta * 5.f, 0.f, 100.f); break;
	case 2: Water->Sim.S.DiffusionRate = FMath::Clamp(Water->Sim.S.DiffusionRate + Delta * 0.05f, 0.f, 1.f); break;
	case 3: Water->ValveThreshold = FMath::Clamp(Water->ValveThreshold + Delta * 0.01f, 0.01f, 0.5f); break;
	default: Water->DefaultGateH = FMath::Clamp(Water->DefaultGateH + Delta * 0.25f, 0.f, 3.f); break;
	}
}

FString AGWPlayerController::OverlayName(int32 I)
{
	static const TCHAR* N[] = { TEXT("自然"), TEXT("水深"), TEXT("流速"), TEXT("污染度"), TEXT("聚簇饱和度"), TEXT("灌溉湿度"), TEXT("土壤污染") };
	return N[FMath::Clamp(I, 0, 6)];
}

void AGWPlayerController::SetOverlay(int32 I)
{
	if (!Water) return;
	const int32 Count = (int32)EGWOverlay::Count;
	I = ((I % Count) + Count) % Count;
	Water->SetOverlay((EGWOverlay)I);
	static const TCHAR* Help[] = {
		TEXT("正常画面。按 V 切换调试视图，F 显示流向箭头。"),
		TEXT("水深：越深越亮。瀑布和坝前的深水区一目了然。"),
		TEXT("流速：由四个管道通量估算（Mei 2007 公式），急流为橙红色。配合 F 的箭头查看方向。"),
		TEXT("污染度：每个水柱上的浓度标量。羽流沿河道平流，在静水中缓慢扩散。"),
		TEXT("聚簇饱和度：自身 3×3 邻域的有水格数与四邻格计数减一取大，上限 8。决定蒸发倍率和灌溉半径。"),
		TEXT("灌溉湿度：水体向外逐格衰减，最远 16 格；向高处传播每升一格扣 6。干旱时从外向内退去。"),
		TEXT("土壤污染：污染度超过 50% 的水使周边 7 格内的土地变为污染土，作物在约 0.3 天内死亡。") };
	Toast(TEXT("视图：") + OverlayName(I), Help[I]);
}

void AGWPlayerController::SetSpeed(int32 I)
{
	if (!Water) return;
	Water->SpeedIndex = FMath::Clamp(I, 0, 3);
	if (I > 0) SpeedBeforePause = I;
}

void AGWPlayerController::NextPhase()
{
	if (!Water) return;
	Water->AdvancePhase();
	const TCHAR* Names[] = { TEXT("温和季"), TEXT("干旱"), TEXT("恶水潮") };
	Toast(FString(TEXT("进入")) + Names[(int32)Water->Phase],
		Water->Phase == EGWPhase::Badtide ? TEXT("水源污染度从 50% 起，半天内沿双曲正割曲线升到 100%。污染羽流会顺着河道推进，看自动阀何时关闭。")
		: Water->Phase == EGWPhase::Drought ? TEXT("水源停止出水，蒸发继续。浅滩先干，水面缩小使聚簇饱和度下降，灌溉范围从外向内收缩。")
		: TEXT("水源恢复。干旱后强度在半天内逐步回升，死去的作物会重新播种。"));
}

void AGWPlayerController::ToggleHazard()
{
	if (!Water) return;
	Water->ToggleNextHazard();
	Toast(TEXT("下一个危险季"), Water->NextHazard == EGWPhase::Drought ? TEXT("干旱：水源会在开始前半天逐渐减弱到 0。") : TEXT("恶水潮：所有水源改为流出恶水。"));
}

void AGWPlayerController::StartGame()
{
	bIntro = false;
	SetSpeed(1);
	Toast(TEXT("目标"), TEXT("撑过 3 个危险季：水库污染度保持在 5% 以下，农田作物存活率不低于 80%。恶水潮来临前，在河道上用阀门、闸门和堤坝截流导流。"));
}

void AGWPlayerController::Toast(const FString& Title, const FString& Body)
{
	ToastTitle = Title; ToastBody = Body; ToastTime = GetWorld()->GetRealTimeSeconds();
}

void AGWPlayerController::SelectTool(EGWTool T)
{
	Tool = (Tool == T) ? EGWTool::None : T;
	bDirManual = false;
	if (Water) Water->bGridLines = Tool != EGWTool::None;
}

// =================================================================================================
// input
// =================================================================================================
float AGWPlayerController::WheelDelta()
{
	float W = GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	if (FMath::IsNearlyZero(W))
	{
		if (WasInputKeyJustPressed(EKeys::MouseScrollUp)) W = 1.f;
		else if (WasInputKeyJustPressed(EKeys::MouseScrollDown)) W = -1.f;
	}
	return W;
}

void AGWPlayerController::HandleKeys(float DeltaTime)
{
	AGWCamera* Cam = GetCam();
	const bool bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	if (bIntro)
	{
		if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar)) StartGame();
		return;
	}
	if (Cam)
	{
		FVector2D In(0, 0);
		if (IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Up)) In.Y += 1;
		if (IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Down)) In.Y -= 1;
		if (IsInputKeyDown(EKeys::D) || IsInputKeyDown(EKeys::Right)) In.X += 1;
		if (IsInputKeyDown(EKeys::A) || IsInputKeyDown(EKeys::Left)) In.X -= 1;
		Cam->Pan(In, DeltaTime);
		if (WasInputKeyJustPressed(EKeys::Q)) Cam->RotateStep(-1);
		if (WasInputKeyJustPressed(EKeys::E)) Cam->RotateStep(1);
		float Mx, My;
		if (GetMousePosition(Mx, My))
		{
			const FVector2D M(Mx, My);
			if (IsInputKeyDown(EKeys::MiddleMouseButton)) Cam->PanPixels(M - LastMouse);
			LastMouse = M;
		}
		const float Wh = WheelDelta();
		if (!FMath::IsNearlyZero(Wh))
		{
			const int32 S = Water && HoverCell != INDEX_NONE ? Water->FindStructure(HoverCell) : INDEX_NONE;
			if (bShift && S != INDEX_NONE && Water->Structures[S].Type == EGWTool::Floodgate) Water->AdjustGate(HoverCell, Wh * 0.25f);
			else Cam->Zoom(Wh);
		}
	}
	static const TPair<FKey, EGWTool> ToolKeys[] = { {EKeys::One, EGWTool::Levee}, {EKeys::Two, EGWTool::Dam}, {EKeys::Three, EGWTool::Floodgate}, {EKeys::Four, EGWTool::Valve},
		{EKeys::Five, EGWTool::Pump}, {EKeys::Six, EGWTool::Dig}, {EKeys::Seven, EGWTool::Raise}, {EKeys::X, EGWTool::Erase} };
	for (const auto& K : ToolKeys) if (WasInputKeyJustPressed(K.Key)) SelectTool(K.Value);
	if (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::RightMouseButton)) SelectTool(EGWTool::None);
	if (WasInputKeyJustPressed(EKeys::R)) { Dir = (Dir + 1) & 3; bDirManual = true; }
	static const FKey FKeys[] = { EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7 };
	for (int32 I = 0; I < NumPrinciples; ++I) if (WasInputKeyJustPressed(FKeys[I])) TogglePrinciple(I);
	if (!Water) return;
	if (WasInputKeyJustPressed(EKeys::V)) SetOverlay((int32)Water->Overlay + (bShift ? -1 : 1));
	if (WasInputKeyJustPressed(EKeys::F)) { Water->bFlowArrows = !Water->bFlowArrows; }
	if (WasInputKeyJustPressed(EKeys::G)) Water->bGridLines = !Water->bGridLines;
	if (WasInputKeyJustPressed(EKeys::SpaceBar)) SetSpeed(Water->SpeedIndex == 0 ? SpeedBeforePause : 0);
	if (WasInputKeyJustPressed(EKeys::Tab)) SetSpeed(Water->SpeedIndex >= 3 ? 1 : Water->SpeedIndex + 1);
	if (WasInputKeyJustPressed(EKeys::T)) NextPhase();
	if (WasInputKeyJustPressed(EKeys::B)) ToggleHazard();
	if (WasInputKeyJustPressed(EKeys::I)) bInspector = !bInspector;
	if (WasInputKeyJustPressed(EKeys::H)) bUiVisible = !bUiVisible;
	if (WasInputKeyJustPressed(EKeys::L)) bLabOpen = !bLabOpen;
	if (WasInputKeyJustPressed(EKeys::Home)) UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
	if (WasInputKeyJustPressed(EKeys::LeftBracket) || WasInputKeyJustPressed(EKeys::RightBracket))
	{
		const float D = WasInputKeyJustPressed(EKeys::LeftBracket) ? -0.25f : 0.25f;
		const int32 S = HoverCell != INDEX_NONE ? Water->FindStructure(HoverCell) : INDEX_NONE;
		if (S != INDEX_NONE && Water->Structures[S].Type == EGWTool::Floodgate) Water->AdjustGate(HoverCell, D);
		else AdjustParam(4, D > 0 ? 1 : -1);
	}

	// placement (dragging works for levees, digging, raising and erasing)
	const bool bDragTool = Tool == EGWTool::Levee || Tool == EGWTool::Dig || Tool == EGWTool::Raise || Tool == EGWTool::Erase;
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && HoverCell != INDEX_NONE)
	{
		DragCells.Reset();
		bDragging = bDragTool;
		ApplyAt(HoverCell, true);
	}
	else if (bDragging && IsInputKeyDown(EKeys::LeftMouseButton) && HoverCell != INDEX_NONE && !DragCells.Contains(HoverCell)) ApplyAt(HoverCell, false);
	if (!IsInputKeyDown(EKeys::LeftMouseButton)) bDragging = false;
}

void AGWPlayerController::ApplyAt(int32 CellIdx, bool bFirst)
{
	DragCells.Add(CellIdx);
	if (Tool == EGWTool::None)
	{
		if (bFirst) Water->ToggleValve(CellIdx);
		return;
	}
	FString Why;
	const bool bOk = Water->CanPlace(Tool, CellIdx, &Why) && Water->Place(Tool, CellIdx, Dir, Water->DefaultGateH);
	UE_LOG(LogTemp, Verbose, TEXT("GW apply %s at %d: %s %s"), *ToolName(Tool), CellIdx, bOk ? TEXT("ok") : TEXT("refused"), *Why);
	if (!bOk && bFirst && !Why.IsEmpty()) Toast(ToolName(Tool), Why);
}

void AGWPlayerController::UpdateHover()
{
	HoverCell = INDEX_NONE; bHoverValid = false; HoverWhy.Reset();
	FVector O, D;
	if (Water && DeprojectMousePositionToWorld(O, D))
	{
		FVector Hit;
		int32 C, Face;
		// raising earth fills the space in front of a clicked cliff face, like placing a block in a voxel game
		if (Water->TraceCell(O, D, C, Hit, &Face)) HoverCell = Tool == EGWTool::Raise ? Face : C;
	}
	if (!Highlight) return;
	if (HoverCell == INDEX_NONE || bIntro) { Highlight->SetActorHiddenInGame(true); return; }
	// valves follow the current at the hovered cell unless the player rotated them
	if ((Tool == EGWTool::Valve || Tool == EGWTool::Dam || Tool == EGWTool::Floodgate) && !bDirManual)
	{
		const float Vx = Water->Sim.VelX[HoverCell], Vy = Water->Sim.VelY[HoverCell];
		if (FMath::Abs(Vx) + FMath::Abs(Vy) > 0.03f) Dir = FMath::Abs(Vx) >= FMath::Abs(Vy) ? (Vx > 0 ? 0 : 2) : (Vy > 0 ? 1 : 3);
	}
	bHoverValid = Tool == EGWTool::None || Water->CanPlace(Tool, HoverCell, &HoverWhy);
	const FLinearColor Col = Tool == EGWTool::None ? FLinearColor(1.f, 0.95f, 0.8f, 0.25f) : bHoverValid ? FLinearColor(0.35f, 1.f, 0.45f, 0.45f) : FLinearColor(1.f, 0.25f, 0.2f, 0.5f);
	if (Water->GhostMID) Water->GhostMID->SetVectorParameterValue(TEXT("Color"), Col);
	Highlight->SetActorHiddenInGame(false);
	Highlight->SetActorLocation(Water->CellCenter(HoverCell, Water->SurfaceZ(HoverCell) + 4.f));
	Highlight->SetActorScale3D(FVector(1.0f, 1.0f, 0.06f));
}

void AGWPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UpdateHover();
	HandleKeys(DeltaTime);
	if (CaptureStep >= 0) TickCapture(DeltaTime);
}

void AGWPlayerController::Press(const FKey& Key, int32 HoldFrames)
{
	InputKey(FInputKeyParams(Key, IE_Pressed, 1.0, false));
	PendingReleases.Add(TPair<FKey, int32>(Key, HoldFrames));
}

bool AGWPlayerController::MouseToCell(int32 CellIdx)
{
	FVector2D S;
	if (!Water || CellIdx < 0 || CellIdx >= Water->Sim.N) return false;
	// aim at the far half of the cell so a raised neighbour between the camera and the cell does not hide it
	FVector Away = GetCam() ? GetCam()->GetActorForwardVector() : FVector::ZeroVector;
	if (GetCam()) Away = FRotator(0.f, GetCam()->GetYaw(), 0.f).Vector();
	if (!ProjectWorldLocationToScreen(Water->CellCenter(CellIdx, Water->SurfaceZ(CellIdx)) + Away * 28.f, S)) return false;
	SetMouseLocation(FMath::RoundToInt(S.X), FMath::RoundToInt(S.Y));
	return true;
}

void AGWPlayerController::Check(const TCHAR* Name, bool bOk, const FString& Detail)
{
	(bOk ? TestPass : TestFail)++;
	UE_LOG(LogTemp, Display, TEXT("GWTEST %s %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), Name, *Detail);
}

// =================================================================================================
// scripted self-test and screenshots: -GWCapture=<dir>
// Every interaction goes through injected key / mouse events, so the real input handling is exercised.
// =================================================================================================
void AGWPlayerController::TickCapture(float DeltaTime)
{
	// release injected keys after a couple of frames
	for (int32 I = PendingReleases.Num() - 1; I >= 0; --I)
		if (--PendingReleases[I].Value <= 0) { InputKey(FInputKeyParams(PendingReleases[I].Key, IE_Released, 0.0, false)); PendingReleases.RemoveAt(I); }

	struct FStep { const TCHAR* Name; float Wait; TFunction<void(AGWPlayerController*)> Setup; TFunction<void(AGWPlayerController*)> Verify; bool bShot; };
	auto CamAt = [](AGWPlayerController* PC, FVector P, float Arm, float Yaw) { if (AGWCamera* C = PC->GetCam()) { C->Focus(P, Arm, Yaw); C->SnapNow(); } };
	auto FindCell = [](AGWPlayerController* PC, TFunction<bool(int32)> Pred) { for (int32 I = 0; I < PC->Water->Sim.N; ++I) if (Pred(I)) return I; return (int32)INDEX_NONE; };
	auto Noop = [](AGWPlayerController*) {};
	auto M = [](AGWPlayerController* PC, const TCHAR* K) { return (int32)PC->Memo.FindRef(K); };
	static TArray<FStep> Steps;
	if (Steps.Num() == 0)
	{
		Steps = {
			{ TEXT("00_intro"), 1.5f, Noop, Noop, true },
			{ TEXT("t_start"), 0.5f, [](AGWPlayerController* PC) { PC->Press(EKeys::Enter); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("enter_starts"), !PC->bIntro && PC->Water->SpeedIndex == 1, FString::Printf(TEXT("intro=%d speed=%d"), PC->bIntro, PC->Water->SpeedIndex)); }, false },
			{ TEXT("01_overview"), 1.5f, [CamAt](AGWPlayerController* PC) { CamAt(PC, FVector(4800, 3500, 450), 12500.f, -90.f); }, Noop, true },
			{ TEXT("t_rotate"), 1.2f, [](AGWPlayerController* PC) { PC->Memo.Add(TEXT("yaw"), PC->GetCam()->GetYaw()); PC->Press(EKeys::Q); },
				[](AGWPlayerController* PC) { const float D = PC->GetCam()->GetYaw() - PC->Memo[TEXT("yaw")]; PC->Check(TEXT("q_rotates_45"), FMath::IsNearlyEqual(D, -45.f, 1.f), FString::Printf(TEXT("delta=%.1f"), D)); }, false },
			{ TEXT("02_rotated"), 1.0f, Noop, Noop, true },
			{ TEXT("t_rotate_back"), 1.2f, [](AGWPlayerController* PC) { PC->Press(EKeys::E); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("e_rotates_back"), FMath::IsNearlyEqual(PC->GetCam()->GetYaw(), PC->Memo[TEXT("yaw")], 1.f), FString::Printf(TEXT("yaw=%.1f"), PC->GetCam()->GetYaw())); }, false },
			{ TEXT("t_zoom"), 1.0f, [](AGWPlayerController* PC) { PC->Memo.Add(TEXT("arm"), PC->GetCam()->Arm->TargetArmLength); PC->Press(EKeys::MouseScrollUp, 1); },
				[](AGWPlayerController* PC) { const float A = PC->GetCam()->Arm->TargetArmLength; PC->Check(TEXT("wheel_zooms_in"), A < PC->Memo[TEXT("arm")] - 100.f, FString::Printf(TEXT("arm %.0f -> %.0f"), PC->Memo[TEXT("arm")], A)); }, false },
			{ TEXT("t_pan"), 0.8f, [](AGWPlayerController* PC) { PC->Memo.Add(TEXT("fx"), PC->GetCam()->GetFocus().X); PC->Press(EKeys::D, 20); },
				[](AGWPlayerController* PC) { const float Dx = PC->GetCam()->GetFocus().X - PC->Memo[TEXT("fx")]; PC->Check(TEXT("d_pans_east"), Dx > 200.f, FString::Printf(TEXT("dx=%.0f"), Dx)); }, false },
			// build a floodgate on the south branch with the mouse
			{ TEXT("t_tool3"), 0.6f, [CamAt, FindCell](AGWPlayerController* PC) {
					const int32 C = FindCell(PC, [PC](int32 I) { const int32 X = I % AGWWorld::GW, Y = I / AGWWorld::GW; return Y >= 44 && Y <= 50 && X > 36 && X < 52 && PC->Water->Sim.Depth[I] > 0.05f && PC->Water->FindStructure(I) == INDEX_NONE && PC->Water->Sim.Depth[I - 1] > 0.05f && PC->Water->Sim.Depth[I + 1] > 0.05f; });
					PC->Memo.Add(TEXT("gate"), C);
					CamAt(PC, PC->Water->CellCenter(C, 300), 2600.f, -90.f);
					PC->Press(EKeys::Three); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("key3_selects_floodgate"), PC->Tool == EGWTool::Floodgate, ToolName(PC->Tool)); }, false },
			{ TEXT("05_floodgate_hover"), 0.6f, [M](AGWPlayerController* PC) { PC->MouseToCell(M(PC, TEXT("gate"))); },
				[M](AGWPlayerController* PC) { PC->Check(TEXT("mouse_hovers_cell"), PC->HoverCell == M(PC, TEXT("gate")), FString::Printf(TEXT("hover=%d want=%d"), PC->HoverCell, M(PC, TEXT("gate")))); }, true },
			{ TEXT("t_place"), 0.6f, [](AGWPlayerController* PC) { PC->Press(EKeys::LeftMouseButton); },
				[M](AGWPlayerController* PC) { const int32 S = PC->Water->FindStructure(M(PC, TEXT("gate"))); PC->Check(TEXT("click_places_floodgate"), S != INDEX_NONE && PC->Water->Structures[S].Type == EGWTool::Floodgate, FString::Printf(TEXT("idx=%d"), S)); }, false },
			{ TEXT("t_gate_up"), 0.5f, [](AGWPlayerController* PC) { PC->Press(EKeys::RightBracket); },
				[M](AGWPlayerController* PC) { const int32 S = PC->Water->FindStructure(M(PC, TEXT("gate"))); PC->Check(TEXT("bracket_raises_gate"), S != INDEX_NONE && FMath::IsNearlyEqual(PC->Water->Structures[S].GateH, 1.75f), FString::Printf(TEXT("h=%.2f"), S != INDEX_NONE ? PC->Water->Structures[S].GateH : -1.f)); }, false },
			{ TEXT("06_floodgate_backwater"), 4.0f, Noop, Noop, true },
			// drag a levee line
			{ TEXT("t_levee_drag"), 0.5f, [](AGWPlayerController* PC) {
					PC->Press(EKeys::One);
					const int32 G = (int32)PC->Memo[TEXT("gate")];
					PC->Memo.Add(TEXT("lv"), G - 3);
					PC->MouseToCell(G - 3); }, Noop, false },
			{ TEXT("t_levee_drag1"), 0.3f, [](AGWPlayerController* PC) { PC->InputKey(FInputKeyParams(EKeys::LeftMouseButton, IE_Pressed, 1.0, false)); }, Noop, false },
			{ TEXT("t_levee_drag2"), 0.3f, [M](AGWPlayerController* PC) { PC->MouseToCell(M(PC, TEXT("lv")) - 1); }, Noop, false },
			{ TEXT("t_levee_drag3"), 0.3f, [M](AGWPlayerController* PC) { PC->MouseToCell(M(PC, TEXT("lv")) - 2); },
				[M](AGWPlayerController* PC) {
					PC->InputKey(FInputKeyParams(EKeys::LeftMouseButton, IE_Released, 0.0, false));
					int32 N = 0; for (int32 K = 0; K < 3; ++K) { const int32 S = PC->Water->FindStructure(M(PC, TEXT("lv")) - K); N += S != INDEX_NONE && PC->Water->Structures[S].Type == EGWTool::Levee; }
					PC->Check(TEXT("drag_places_levees"), N == 3, FString::Printf(TEXT("levees=%d"), N)); }, false },
			{ TEXT("07_structures"), 1.0f, [M](AGWPlayerController* PC) { PC->Press(EKeys::X); PC->MouseToCell(M(PC, TEXT("lv")) - 2); }, Noop, true },
			{ TEXT("t_erase"), 0.5f, [](AGWPlayerController* PC) { PC->Press(EKeys::LeftMouseButton); },
				[M](AGWPlayerController* PC) { PC->Check(TEXT("x_erases"), PC->Water->FindStructure(M(PC, TEXT("lv")) - 2) == INDEX_NONE, TEXT("")); }, false },
			// valve modes by clicking with the inspect tool
			{ TEXT("t_valve_click"), 0.6f, [CamAt](AGWPlayerController* PC) {
					PC->Press(EKeys::Escape);
					const int32 Si = PC->Water->Structures.IndexOfByPredicate([](const FGWStructure& S) { return S.Type == EGWTool::Valve; });
					const int32 C = PC->Water->Structures[Si].Cell;
					PC->Memo.Add(TEXT("valve"), C);
					CamAt(PC, PC->Water->CellCenter(C, 400), 2600.f, -90.f); }, Noop, false },
			{ TEXT("t_valve_click2"), 0.5f, [M](AGWPlayerController* PC) { PC->MouseToCell(M(PC, TEXT("valve"))); PC->Press(EKeys::LeftMouseButton); },
				[M](AGWPlayerController* PC) { const int32 S = PC->Water->FindStructure(M(PC, TEXT("valve"))); PC->Check(TEXT("click_valve_manual_open"), S != INDEX_NONE && !PC->Water->Structures[S].bAuto && !PC->Water->Structures[S].bClosed, TEXT("")); }, false },
			{ TEXT("t_valve_click3"), 0.5f, [](AGWPlayerController* PC) { PC->Press(EKeys::LeftMouseButton); },
				[M](AGWPlayerController* PC) { const int32 S = PC->Water->FindStructure(M(PC, TEXT("valve"))); PC->Check(TEXT("click_valve_closed"), S != INDEX_NONE && PC->Water->Structures[S].bClosed && !PC->Water->Structures[S].bAuto, TEXT("")); }, false },
			{ TEXT("t_valve_click4"), 0.5f, [](AGWPlayerController* PC) { PC->Press(EKeys::LeftMouseButton); },
				[M](AGWPlayerController* PC) { const int32 S = PC->Water->FindStructure(M(PC, TEXT("valve"))); PC->Check(TEXT("click_valve_back_to_auto"), S != INDEX_NONE && PC->Water->Structures[S].bAuto, TEXT("")); }, false },
			// principle switches F1..F7 and back
			{ TEXT("t_principles"), 0.4f, [](AGWPlayerController* PC) { for (const FKey& K : { EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7 }) PC->Press(K); },
				[](AGWPlayerController* PC) { const FGWSimSettings& S = PC->Water->Sim.S;
					PC->Check(TEXT("f_keys_toggle"), !S.bKScaling && !S.bSoftDam && !S.bSpillThreshold && S.Contam == EGWContam::MassConserving && !S.bSaturationEvaporation && S.Damping == EGWDamping::Strong && S.Diffusion == EGWDiffusion::SmoothGate, TEXT("")); }, false },
			{ TEXT("08_lab_changed"), 3.0f, [CamAt](AGWPlayerController* PC) { CamAt(PC, FVector(4800, 3500, 450), 12500.f, -90.f); }, Noop, true },
			{ TEXT("t_principles_back"), 0.4f, [](AGWPlayerController* PC) { for (const FKey& K : { EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7 }) PC->Press(K); }, Noop, false },
			{ TEXT("t_principles_back2"), 0.4f, [](AGWPlayerController* PC) { PC->Press(EKeys::F2); PC->Press(EKeys::F6); }, Noop, false },
			{ TEXT("t_principles_back3"), 0.4f, [](AGWPlayerController* PC) { PC->Press(EKeys::F6); },
				[](AGWPlayerController* PC) { const FGWSimSettings& S = PC->Water->Sim.S;
					PC->Check(TEXT("f_keys_restore"), S.bKScaling && S.bSoftDam && S.bSpillThreshold && S.Contam == EGWContam::GameEffective && S.bSaturationEvaporation && S.Damping == EGWDamping::Game0999 && S.Diffusion == EGWDiffusion::BoolGate, TEXT(""));
					PC->Water->Sim.ResetLedger(); }, false },
			{ TEXT("t_overlay"), 0.4f, [](AGWPlayerController* PC) { PC->Press(EKeys::V); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("v_cycles_overlay"), PC->Water->Overlay == EGWOverlay::Depth, OverlayName((int32)PC->Water->Overlay)); }, false },
			{ TEXT("09_depth"), 1.0f, Noop, Noop, true },
			{ TEXT("t_speed"), 0.4f, [](AGWPlayerController* PC) { PC->SetOverlay(0); PC->Press(EKeys::SpaceBar); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("space_pauses"), PC->Water->SpeedIndex == 0, TEXT("")); }, false },
			{ TEXT("t_speed2"), 0.4f, [](AGWPlayerController* PC) { PC->Press(EKeys::SpaceBar); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("space_resumes"), PC->Water->SpeedIndex == 1, TEXT("")); }, false },
			{ TEXT("t_speed3"), 0.4f, [](AGWPlayerController* PC) { PC->Press(EKeys::Tab); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("tab_speeds_up"), PC->Water->SpeedIndex == 2, TEXT("")); }, false },
			{ TEXT("t_dig"), 0.6f, [CamAt, FindCell](AGWPlayerController* PC) {
					PC->Press(EKeys::Six);
					const int32 C = FindCell(PC, [PC](int32 I) { const int32 X = I % AGWWorld::GW, Y = I / AGWWorld::GW; const FWaterSim& Sm = PC->Water->Sim; if (Y < 30 || Y > 50 || X < 20 || X > 70 || Sm.Depth[I] > 0.f || PC->Water->FindStructure(I) != INDEX_NONE) return false;
						for (int32 Oy = -2; Oy <= 2; ++Oy) for (int32 Ox = -2; Ox <= 2; ++Ox) if (Sm.Ground[I + Oy * AGWWorld::GW + Ox] != Sm.Ground[I] || Sm.Depth[I + Oy * AGWWorld::GW + Ox] > 0.f) return false;
						return true; });
					PC->Memo.Add(TEXT("dig"), C); PC->Memo.Add(TEXT("digh"), PC->Water->Sim.Ground[C]);
					UE_LOG(LogTemp, Display, TEXT("GWTEST dig cell %d ground %.0f"), C, PC->Water->Sim.Ground[C]);
					CamAt(PC, PC->Water->CellCenter(C, 300), 2600.f, -90.f); }, Noop, false },
			{ TEXT("t_dig1"), 0.4f, [M](AGWPlayerController* PC) { PC->MouseToCell(M(PC, TEXT("dig"))); }, Noop, false },
			{ TEXT("t_dig2"), 0.5f, [](AGWPlayerController* PC) { PC->Press(EKeys::LeftMouseButton); },
				[M](AGWPlayerController* PC) { const int32 C = M(PC, TEXT("dig")); PC->Check(TEXT("dig_lowers_ground"), PC->Water->Sim.Ground[C] == PC->Memo[TEXT("digh")] - 1.f, FString::Printf(TEXT("%.0f -> %.0f"), PC->Memo[TEXT("digh")], PC->Water->Sim.Ground[C])); }, false },
			{ TEXT("t_raise"), 0.4f, [M](AGWPlayerController* PC) {
					// a one-cell pit is hidden behind its near rim at this pitch: click the visible far wall, as a player would
					PC->Press(EKeys::Seven);
					const int32 C = M(PC, TEXT("dig"));
					const FVector Away = FRotator(0.f, PC->GetCam()->GetYaw(), 0.f).Vector();
					FVector2D Sc;
					if (PC->ProjectWorldLocationToScreen(PC->Water->CellCenter(C, PC->Water->SurfaceZ(C) + 55.f) + Away * 49.f, Sc)) PC->SetMouseLocation(FMath::RoundToInt(Sc.X), FMath::RoundToInt(Sc.Y)); }, Noop, false },
			{ TEXT("t_raise2"), 0.5f, [](AGWPlayerController* PC) { PC->Press(EKeys::LeftMouseButton); },
				[M](AGWPlayerController* PC) { const int32 C = M(PC, TEXT("dig")); PC->Check(TEXT("raise_restores_ground"), PC->Water->Sim.Ground[C] == PC->Memo[TEXT("digh")], FString::Printf(TEXT("%.0f tool=%s hover=%d cell=%d"), PC->Water->Sim.Ground[C], *ToolName(PC->Tool), PC->HoverCell, C)); }, false },
			// seasons: badtide with the default valve protecting the reservoir
			{ TEXT("10_badtide_start"), 3.0f, [CamAt](AGWPlayerController* PC) { PC->Press(EKeys::Escape); PC->Press(EKeys::T); CamAt(PC, FVector(3800, 2600, 450), 7600.f, -90.f); }, Noop, true },
			{ TEXT("11_badtide_valve"), 3.0f, [CamAt, M](AGWPlayerController* PC) { CamAt(PC, PC->Water->CellCenter(M(PC, TEXT("valve")), 400), 3000.f, -135.f); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("badtide_phase"), PC->Water->Phase == EGWPhase::Badtide, TEXT("")); }, true },
			{ TEXT("12_contamination"), 2.5f, [CamAt](AGWPlayerController* PC) { PC->SetOverlay(3); CamAt(PC, FVector(5200, 3600, 450), 9000.f, -90.f); },
				[M](AGWPlayerController* PC) {
					int32 Wet = 0, Closed = 0;
					for (const FGWStructure& St : PC->Water->Structures)
					{
						if (St.Type != EGWTool::Valve) continue;
						const int32 Up = St.Cell - (St.Dir == 0 ? 1 : St.Dir == 2 ? -1 : St.Dir == 1 ? AGWWorld::GW : -AGWWorld::GW);
						if (PC->Water->Sim.Depth[Up] > 0.02f && PC->Water->Sim.Conc[Up] > 0.06f) { ++Wet; Closed += St.bClosed; }
					}
					PC->Check(TEXT("valves_close_on_badwater"), Wet > 0 && Wet == Closed, FString::Printf(TEXT("contaminated upstream %d, closed %d"), Wet, Closed));
					PC->Check(TEXT("reservoir_clean"), PC->Water->ReservoirContamination() < 0.05f, FString::Printf(TEXT("c=%.3f"), PC->Water->ReservoirContamination())); }, true },
			{ TEXT("13_soil"), 2.5f, [](AGWPlayerController* PC) { PC->SetOverlay(6); }, Noop, true },
			{ TEXT("14_drought_a"), 1.0f, [](AGWPlayerController* PC) { PC->SetOverlay(0); PC->Press(EKeys::T); }, Noop, false },
			{ TEXT("14_drought"), 4.0f, [](AGWPlayerController* PC) { PC->Water->NextHazard = EGWPhase::Drought; PC->Press(EKeys::T); },
				[](AGWPlayerController* PC) { PC->Check(TEXT("drought_phase"), PC->Water->Phase == EGWPhase::Drought, TEXT("")); }, true },
			{ TEXT("15_moisture_drought"), 2.0f, [](AGWPlayerController* PC) { PC->SetOverlay(5); }, Noop, true },
			{ TEXT("t_conservation"), 0.2f, Noop, [](AGWPlayerController* PC) { PC->Check(TEXT("water_conserved"), FMath::Abs(PC->Water->Sim.WaterError()) < 0.5, FString::Printf(TEXT("err=%.4f"), PC->Water->Sim.WaterError())); }, false },
			{ TEXT("t_k_off"), 3.0f, [](AGWPlayerController* PC) { PC->SetOverlay(0); PC->Press(EKeys::T); PC->Press(EKeys::F1); }, Noop, false },
			{ TEXT("16_k_off"), 2.0f, Noop, [](AGWPlayerController* PC) { PC->Check(TEXT("k_off_breaks_conservation"), PC->Water->Sim.WaterError() > 1.0, FString::Printf(TEXT("err=%.3f"), PC->Water->Sim.WaterError())); }, true },
		};
	}
	if (CaptureStep >= Steps.Num())
	{
		UE_LOG(LogTemp, Display, TEXT("GWTEST SUMMARY pass=%d fail=%d"), TestPass, TestFail);
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		CaptureStep = -1;
		return;
	}
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) { CaptureTimer = 0.f; CaptureFrames = 0; return; }
	const FStep& S = Steps[CaptureStep];
	if (CaptureFrames == 0) S.Setup(this);
	++CaptureFrames;
	CaptureTimer += FMath::Min(DeltaTime, 0.05f);
	if (!bShotPending && CaptureTimer > S.Wait && CaptureFrames > 8)
	{
		S.Verify(this);
		if (S.bShot) FScreenshotRequest::RequestScreenshot(CaptureDir / FString(S.Name) + TEXT(".png"), true, false);
		bShotPending = true;
		UE_LOG(LogTemp, Display, TEXT("GWCapture %s water %.1f err %.3f contam %.2f err %.4f res %.1f c %.3f"), S.Name, Water ? Water->Sim.TotalWater() : 0.0,
			Water ? Water->Sim.WaterError() : 0.0, Water ? Water->Sim.TotalContaminant() : 0.0, Water ? Water->Sim.ContamError() : 0.0,
			Water ? Water->ReservoirVolume() : 0.f, Water ? Water->ReservoirContamination() : 0.f);
	}
	if (bShotPending && CaptureTimer > S.Wait + (S.bShot ? 0.4f : 0.05f))
	{
		bShotPending = false; CaptureTimer = 0.f; CaptureFrames = 0; ++CaptureStep;
	}
}
