#include "GWHud.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GWPlayerController.h"
#include "GWWorld.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Walnut panels with a brass rim and cream ink: warm, wooden, reads over both green valleys and red badtide light.
	const FLinearColor Fill(0.030f, 0.020f, 0.012f, 0.90f);
	const FLinearColor FillSoft(0.045f, 0.032f, 0.020f, 0.82f);
	const FLinearColor Rim(0.50f, 0.33f, 0.13f, 0.90f);
	const FLinearColor Row(0.060f, 0.043f, 0.028f, 0.95f);
	const FLinearColor RowSel(0.42f, 0.27f, 0.07f, 1.f);
	const FLinearColor Ink(0.96f, 0.92f, 0.84f);
	const FLinearColor Dim(0.70f, 0.64f, 0.56f);
	const FLinearColor Gold(1.00f, 0.72f, 0.28f);
	const FLinearColor Good(0.45f, 0.86f, 0.42f);
	const FLinearColor Bad(1.00f, 0.42f, 0.30f);
	const FLinearColor Teal(0.30f, 0.80f, 0.80f);
	const FLinearColor CapFill(0.88f, 0.82f, 0.70f);
	const FLinearColor CapInk(0.06f, 0.045f, 0.03f);
	const FLinearColor DayTemperate(0.30f, 0.62f, 0.30f), DayDrought(0.85f, 0.58f, 0.18f), DayBadtide(0.62f, 0.18f, 0.10f);

	FSlateBrush Rounded(FLinearColor F, float R, FLinearColor O = FLinearColor::Transparent, float W = 0.f)
	{
		FSlateRoundedBoxBrush B(F, R);
		B.OutlineSettings = FSlateBrushOutlineSettings(FVector4(R, R, R, R), FSlateColor(O), W);
		return B;
	}

	const EGWTool ToolOrder[] = { EGWTool::None, EGWTool::Levee, EGWTool::Dam, EGWTool::Floodgate, EGWTool::Valve, EGWTool::Pump, EGWTool::Dig, EGWTool::Raise, EGWTool::Erase };

	FLinearColor Ramp4(const FLinearColor S[4], float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f) * 3.f;
		const int32 I = FMath::Min((int32)T, 2);
		return FMath::Lerp(S[I], S[I + 1], T - I);
	}
	// keep in sync with OVERLAY_RAMPS in Tools/build_assets.py
	const FLinearColor RampDepth[4] = { {0.02f, 0.05f, 0.15f}, {0.05f, 0.35f, 0.65f}, {0.30f, 0.85f, 0.95f}, {0.95f, 1.f, 1.f} };
	const FLinearColor RampSpeed[4] = { {0.15f, 0.20f, 0.60f}, {0.10f, 0.70f, 0.60f}, {0.95f, 0.85f, 0.20f}, {0.90f, 0.25f, 0.10f} };
	const FLinearColor RampContam[4] = { {0.10f, 0.45f, 0.80f}, {0.95f, 0.85f, 0.25f}, {0.85f, 0.35f, 0.10f}, {0.45f, 0.10f, 0.05f} };
	const FLinearColor RampSat[4] = { {0.27f, 0.00f, 0.33f}, {0.23f, 0.32f, 0.55f}, {0.13f, 0.57f, 0.55f}, {0.99f, 0.90f, 0.14f} };
	const FLinearColor RampMoist[4] = { {0.45f, 0.42f, 0.38f}, {0.75f, 0.85f, 0.45f}, {0.30f, 0.65f, 0.25f}, {0.05f, 0.35f, 0.15f} };
	const FLinearColor RampSoil[4] = { {0.45f, 0.42f, 0.38f}, {0.60f, 0.45f, 0.60f}, {0.55f, 0.20f, 0.55f}, {0.35f, 0.05f, 0.35f} };
}

void UGWClick::OnClicked()
{
	AGWPlayerController* P = Owner.Get();
	if (!P || !P->GetWater()) return;
	AGWWorld* W = P->GetWater();
	switch (Kind)
	{
	case 0: P->SelectTool(ToolOrder[Index]); break;
	case 1: P->TogglePrinciple(Index); break;
	case 2: P->AdjustParam(Index, -1); break;
	case 3: P->AdjustParam(Index, 1); break;
	case 4: P->SetOverlay(Index); break;
	case 5: P->SetSpeed(Index); break;
	case 6: P->NextPhase(); break;
	case 7: P->ToggleHazard(); break;
	case 8: P->StartGame(); break;
	case 9: P->bLabOpen = !P->bLabOpen; break;
	case 10: P->AdjustSelection(-1); break;
	case 11: P->AdjustSelection(1); break;
	case 12: P->SetSelectedValveMode(Index); break;
	case 13: P->RemoveSelection(); break;
	case 14: P->ClearSelection(); break;
	default: break;
	}
}

AGWPlayerController* UGWHud::PC() const { return Cast<AGWPlayerController>(GetOwningPlayer()); }

UTextBlock* UGWHud::Text(int32 Size, FLinearColor Color, bool bBold, bool bWrap, float WrapAt)
{
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
	T->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
	T->SetColorAndOpacity(FSlateColor(Color));
	T->SetShadowOffset(FVector2D(1.f, 1.f));
	T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	if (bWrap) { T->SetAutoWrapText(WrapAt <= 0.f); if (WrapAt > 0.f) T->SetWrapTextAt(WrapAt); }
	return T;
}

UBorder* UGWHud::Panel(FLinearColor F, FLinearColor R, float Radius, FMargin Pad)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrush(Rounded(F, Radius, R, R.A > 0.f ? 1.5f : 0.f));
	B->SetPadding(Pad);
	return B;
}

UBorder* UGWHud::KeyCap(const FString& Key, int32 Size)
{
	UBorder* Cap = Panel(CapFill, FLinearColor(0.36f, 0.27f, 0.16f, 1.f), 4.f, FMargin(7, 1, 7, 2));
	UTextBlock* T = Text(Size, CapInk, true);
	T->SetShadowColorAndOpacity(FLinearColor::Transparent);
	T->SetText(FText::FromString(Key));
	T->SetJustification(ETextJustify::Center);
	Cap->SetContent(T);
	Cap->SetHorizontalAlignment(HAlign_Center);
	Cap->SetVerticalAlignment(VAlign_Center);
	return Cap;
}

UButton* UGWHud::Button(int32 Kind, int32 Index, FLinearColor F)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	FButtonStyle St;
	St.SetNormal(Rounded(FLinearColor(0, 0, 0, 0), 6.f));
	St.SetHovered(Rounded(FLinearColor(1.f, 0.85f, 0.6f, 0.08f), 6.f, Gold, 1.f));
	St.SetPressed(Rounded(FLinearColor(1.f, 0.85f, 0.6f, 0.16f), 6.f, Gold, 1.f));
	St.SetNormalPadding(FMargin(0)); St.SetPressedPadding(FMargin(0));
	B->SetStyle(St);
	UGWClick* H = NewObject<UGWClick>(this);
	H->Kind = Kind; H->Index = Index; H->Owner = PC();
	B->OnClicked.AddDynamic(H, &UGWClick::OnClicked);
	Clicks.Add(H);
	return B;
}

UCanvasPanelSlot* UGWHud::Put(UWidget* W, FVector2D Anchor, FVector2D Align, FVector2D Pos)
{
	UCanvasPanelSlot* S = Canvas->AddChildToCanvas(W);
	S->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
	S->SetAlignment(Align);
	S->SetPosition(Pos);
	S->SetAutoSize(true);
	return S;
}

TSharedRef<SWidget> UGWHud::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto VBox = [this]() { return WidgetTree->ConstructWidget<UVerticalBox>(); };
		auto HBox = [this]() { return WidgetTree->ConstructWidget<UHorizontalBox>(); };
		auto Label = [this](const FString& S, int32 Size, FLinearColor C, bool bBold = false) { UTextBlock* T = Text(Size, C, bBold); T->SetText(FText::FromString(S)); return T; };
		auto Gap = [this](float W, float H) { USpacer* S = WidgetTree->ConstructWidget<USpacer>(); S->SetSize(FVector2D(W, H)); return S; };
		auto Sized = [this](UWidget* W, float Wd, float Ht) { USizeBox* B = WidgetTree->ConstructWidget<USizeBox>(); if (Wd > 0) B->SetWidthOverride(Wd); if (Ht > 0) B->SetHeightOverride(Ht); B->SetContent(W); return B; };

		// ---------------- objective card (top-left) ----------------
		{
			UBorder* Card = Panel(Fill, Rim, 10.f, FMargin(16, 12, 18, 12));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("河谷水利 · 网格水模拟"), 21, Gold, true));
			V->AddChildToVerticalBox(Label(TEXT("《海狸浮生记》水系统复现 · GameTech-Lab"), 12, Dim));
			V->AddChildToVerticalBox(Gap(1, 8));
			ObjHazards = Text(15, Ink); ObjReservoir = Text(15, Ink); ObjCrops = Text(15, Ink);
			V->AddChildToVerticalBox(ObjHazards); V->AddChildToVerticalBox(ObjReservoir); V->AddChildToVerticalBox(ObjCrops);
			Card->SetContent(V);
			Put(Card, FVector2D(0, 0), FVector2D(0, 0), FVector2D(18, 16));
			GameplayWidgets.Add(Card);
		}
		// ---------------- season timeline (top-center) ----------------
		{
			UBorder* Card = Panel(Fill, Rim, 10.f, FMargin(18, 10, 18, 10));
			UVerticalBox* V = VBox();
			UHorizontalBox* Top = HBox();
			SeasonTitle = Text(20, Ink, true);
			Top->AddChildToHorizontalBox(SeasonTitle);
			Top->AddChildToHorizontalBox(Gap(14, 1));
			SeasonSub = Text(15, Dim);
			Top->AddChildToHorizontalBox(SeasonSub)->SetVerticalAlignment(VAlign_Center);
			V->AddChildToVerticalBox(Top);
			V->AddChildToVerticalBox(Gap(1, 6));
			UHorizontalBox* Days = HBox();
			for (int32 D = 0; D < 12; ++D)
			{
				UBorder* C = Panel(DayTemperate, FLinearColor::Transparent, 3.f, FMargin(0));
				DayCells.Add(C);
				Days->AddChildToHorizontalBox(Sized(C, 44, 12))->SetPadding(FMargin(D == 6 ? 10 : 2, 0, 2, 0));
			}
			V->AddChildToVerticalBox(Days);
			V->AddChildToVerticalBox(Gap(1, 7));
			UHorizontalBox* Ctl = HBox();
			const TCHAR* Sp[] = { TEXT("暂停"), TEXT("×1"), TEXT("×2"), TEXT("×4") };
			for (int32 I = 0; I < 4; ++I)
			{
				UButton* B = Button(5, I, Row);
				UBorder* Back = Panel(Row, FLinearColor::Transparent, 5.f, FMargin(10, 2, 10, 3));
				Back->SetContent(Label(Sp[I], 14, Ink, true));
				B->SetContent(Back);
				SpeedBacks.Add(Back);
				Ctl->AddChildToHorizontalBox(B)->SetPadding(FMargin(0, 0, 4, 0));
			}
			Ctl->AddChildToHorizontalBox(Gap(10, 1));
			{
				UButton* B = Button(6, 0, Row);
				UBorder* Back = Panel(Row, FLinearColor::Transparent, 5.f, FMargin(10, 2, 10, 3));
				Back->SetContent(Label(TEXT("跳到下一季  T"), 14, Ink));
				B->SetContent(Back);
				Ctl->AddChildToHorizontalBox(B)->SetPadding(FMargin(0, 0, 4, 0));
			}
			{
				UButton* B = Button(7, 0, Row);
				UBorder* Back = Panel(Row, FLinearColor::Transparent, 5.f, FMargin(10, 2, 10, 3));
				HazardText = Text(14, Ink);
				Back->SetContent(HazardText);
				B->SetContent(Back);
				Ctl->AddChildToHorizontalBox(B);
			}
			V->AddChildToVerticalBox(Ctl);
			Card->SetContent(V);
			Put(Card, FVector2D(0.5f, 0), FVector2D(0.5f, 0), FVector2D(0, 16));
			GameplayWidgets.Add(Card);
		}
		// ---------------- build palette (left) ----------------
		{
			UBorder* Card = Panel(Fill, Rim, 10.f, FMargin(10, 10, 10, 12));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("建造"), 16, Gold, true))->SetPadding(FMargin(4, 0, 0, 6));
			for (int32 I = 0; I < UE_ARRAY_COUNT(ToolOrder); ++I)
			{
				UButton* B = Button(0, I, Row);
				UBorder* Back = Panel(Row, FLinearColor::Transparent, 6.f, FMargin(6, 4, 10, 4));
				UHorizontalBox* H = HBox();
				H->AddChildToHorizontalBox(Sized(KeyCap(AGWPlayerController::ToolKey(ToolOrder[I]), 12), 40, 0))->SetVerticalAlignment(VAlign_Center);
				H->AddChildToHorizontalBox(Gap(8, 1));
				H->AddChildToHorizontalBox(Label(AGWPlayerController::ToolName(ToolOrder[I]), 16, Ink))->SetVerticalAlignment(VAlign_Center);
				Back->SetContent(Sized(H, 150, 0));
				B->SetContent(Back);
				ToolBacks.Add(Back);
				V->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 0, 0, 3));
			}
			V->AddChildToVerticalBox(Gap(1, 6));
			ToolHelp = Text(13, Dim, false, true, 168.f);
			V->AddChildToVerticalBox(Sized(ToolHelp, 172, 0));
			ToolStatus = Text(13, Bad, true, true, 168.f);
			V->AddChildToVerticalBox(Sized(ToolStatus, 172, 0))->SetPadding(FMargin(0, 4, 0, 0));
			Card->SetContent(V);
			Put(Card, FVector2D(0, 0), FVector2D(0, 0), FVector2D(18, 172));
			GameplayWidgets.Add(Card);
		}
		// ---------------- principle lab (right) ----------------
		{
			LabPanel = Panel(Fill, Rim, 10.f, FMargin(14, 10, 14, 12));
			UVerticalBox* V = VBox();
			UHorizontalBox* Head = HBox();
			Head->AddChildToHorizontalBox(Label(TEXT("原理实验室"), 18, Gold, true));
			Head->AddChildToHorizontalBox(Gap(8, 1));
			Head->AddChildToHorizontalBox(Label(TEXT("切换后观察画面与左下角账本"), 12, Dim))->SetVerticalAlignment(VAlign_Center);
			V->AddChildToVerticalBox(Head)->SetPadding(FMargin(0, 0, 0, 6));
			for (int32 I = 0; I < AGWPlayerController::NumPrinciples; ++I)
			{
				UButton* B = Button(1, I, Row);
				UBorder* Back = Panel(Row, FLinearColor::Transparent, 6.f, FMargin(6, 4, 6, 4));
				UHorizontalBox* H = HBox();
				H->AddChildToHorizontalBox(Sized(KeyCap(FString::Printf(TEXT("F%d"), I + 1), 11), 38, 0))->SetVerticalAlignment(VAlign_Center);
				H->AddChildToHorizontalBox(Gap(8, 1));
				UTextBlock* N = Label(PC() ? PC()->PrincipleName(I) : FString(), 14, Ink);
				H->AddChildToHorizontalBox(Sized(N, 132, 0))->SetVerticalAlignment(VAlign_Center);
				UBorder* Pill = Panel(RowSel, FLinearColor::Transparent, 9.f, FMargin(9, 1, 9, 2));
				UTextBlock* Val = Text(13, Ink, true);
				Pill->SetContent(Val);
				PrincipleValues.Add(Val); PrinciplePills.Add(Pill);
				H->AddChildToHorizontalBox(Pill)->SetVerticalAlignment(VAlign_Center);
				Back->SetContent(Sized(H, 350, 0));
				B->SetContent(Back);
				V->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 0, 0, 2));
			}
			V->AddChildToVerticalBox(Gap(1, 6));
			UHorizontalBox* VH = HBox();
			VH->AddChildToHorizontalBox(Label(TEXT("调试视图"), 15, Gold, true));
			VH->AddChildToHorizontalBox(Gap(8, 1));
			VH->AddChildToHorizontalBox(KeyCap(TEXT("V"), 11))->SetVerticalAlignment(VAlign_Center);
			VH->AddChildToHorizontalBox(Gap(6, 1));
			VH->AddChildToHorizontalBox(Label(TEXT("流向箭头"), 13, Dim))->SetVerticalAlignment(VAlign_Center);
			VH->AddChildToHorizontalBox(Gap(4, 1));
			VH->AddChildToHorizontalBox(KeyCap(TEXT("F"), 11))->SetVerticalAlignment(VAlign_Center);
			V->AddChildToVerticalBox(VH)->SetPadding(FMargin(0, 0, 0, 4));
			for (int32 Rw = 0; Rw < 2; ++Rw)
			{
				UHorizontalBox* H = HBox();
				for (int32 K = 0; K < 4; ++K)
				{
					const int32 I = Rw * 4 + K;
					if (I >= (int32)EGWOverlay::Count) break;
					UButton* B = Button(4, I, Row);
					UBorder* Back = Panel(Row, FLinearColor::Transparent, 5.f, FMargin(8, 2, 8, 3));
					Back->SetContent(Label(AGWPlayerController::OverlayName(I), 13, Ink));
					B->SetContent(Back);
					OverlayBacks.Add(Back);
					H->AddChildToHorizontalBox(B)->SetPadding(FMargin(0, 0, 4, 4));
				}
				V->AddChildToVerticalBox(H);
			}
			V->AddChildToVerticalBox(Gap(1, 4));
			V->AddChildToVerticalBox(Label(TEXT("参数"), 15, Gold, true))->SetPadding(FMargin(0, 0, 0, 4));
			for (int32 I = 0; I < AGWPlayerController::NumParams; ++I)
			{
				UHorizontalBox* H = HBox();
				H->AddChildToHorizontalBox(Sized(Label(PC() ? PC()->ParamName(I) : FString(), 14, Ink), 132, 0))->SetVerticalAlignment(VAlign_Center);
				for (int32 Sgn = 0; Sgn < 2; ++Sgn)
				{
					UButton* B = Button(Sgn ? 3 : 2, I, Row);
					UBorder* Back = Panel(Row, Rim, 5.f, FMargin(9, 0, 9, 1));
					Back->SetContent(Label(Sgn ? TEXT("+") : TEXT("−"), 15, Gold, true));
					B->SetContent(Back);
					if (Sgn == 0) H->AddChildToHorizontalBox(B);
					else
					{
						UTextBlock* Val = Text(14, Ink, true);
						Val->SetJustification(ETextJustify::Center);
						ParamValues.Add(Val);
						H->AddChildToHorizontalBox(Sized(Val, 110, 0))->SetVerticalAlignment(VAlign_Center);
						H->AddChildToHorizontalBox(B);
					}
				}
				V->AddChildToVerticalBox(H)->SetPadding(FMargin(0, 0, 0, 3));
			}
			LabPanel->SetContent(V);
			Put(LabPanel, FVector2D(1, 0.5f), FVector2D(1, 0.5f), FVector2D(-18, 10));
			GameplayWidgets.Add(LabPanel);
		}
		// ---------------- ledger (bottom-left) ----------------
		{
			UBorder* Card = Panel(FillSoft, Rim, 10.f, FMargin(14, 10, 16, 10));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("水量账本"), 15, Gold, true))->SetPadding(FMargin(0, 0, 0, 3));
			const TCHAR* Names[] = { TEXT("网格内水量"), TEXT("累计注入"), TEXT("流出地图"), TEXT("蒸发"), TEXT("抽取与移除"), TEXT("守恒误差"),
				TEXT("污染物总量"), TEXT("污染物误差"), TEXT("水库水量"), TEXT("水泵抽到恶水") };
			for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
			{
				if (I == 6) V->AddChildToVerticalBox(Gap(1, 4));
				UHorizontalBox* H = HBox();
				H->AddChildToHorizontalBox(Sized(Label(Names[I], 13, Dim), 100, 0));
				UTextBlock* Val = Text(13, Ink, true);
				Val->SetJustification(ETextJustify::Right);
				LedgerValues.Add(Val);
				H->AddChildToHorizontalBox(Sized(Val, 120, 0));
				V->AddChildToVerticalBox(H);
			}
			Card->SetContent(V);
			Put(Card, FVector2D(0, 1), FVector2D(0, 1), FVector2D(18, -66));
			GameplayWidgets.Add(Card);
		}
		// ---------------- hint bar (bottom) ----------------
		{
			UBorder* Bar = Panel(FillSoft, FLinearColor::Transparent, 8.f, FMargin(12, 6, 12, 6));
			UHorizontalBox* H = HBox();
			const TPair<const TCHAR*, const TCHAR*> Hints[] = { {TEXT("WASD"), TEXT("平移")}, {TEXT("R"), TEXT("旋转 45°")}, {TEXT("滚轮"), TEXT("缩放")}, {TEXT("中键"), TEXT("拖动")},
				{TEXT("1–7"), TEXT("建造")}, {TEXT("Z"), TEXT("建筑转向")}, {TEXT("[ ]"), TEXT("闸高")}, {TEXT("V"), TEXT("视图")}, {TEXT("Space"), TEXT("暂停")}, {TEXT("Tab"), TEXT("速度")},
				{TEXT("I"), TEXT("格子信息")}, {TEXT("L"), TEXT("实验室")}, {TEXT("H"), TEXT("隐藏界面")}, {TEXT("Home"), TEXT("重开")} };
			for (const auto& P : Hints)
			{
				H->AddChildToHorizontalBox(KeyCap(P.Key, 12))->SetVerticalAlignment(VAlign_Center);
				H->AddChildToHorizontalBox(Label(P.Value, 14, Ink))->SetPadding(FMargin(5, 0, 14, 0));
			}
			Bar->SetContent(H);
			Put(Bar, FVector2D(0.5f, 1), FVector2D(0.5f, 1), FVector2D(0, -14));
			GameplayWidgets.Add(Bar);
		}
		// ---------------- selection panel (above the hint bar) ----------------
		{
			SelPanel = Panel(Fill, Gold, 10.f, FMargin(16, 10, 16, 10));
			UHorizontalBox* H = HBox();
			UVerticalBox* Left = VBox();
			SelTitle = Text(17, Gold, true);
			Left->AddChildToVerticalBox(SelTitle);
			Left->AddChildToVerticalBox(Label(TEXT("相连的同类建筑同步调整"), 13, Dim));
			H->AddChildToHorizontalBox(Left)->SetVerticalAlignment(VAlign_Center);
			H->AddChildToHorizontalBox(Gap(22, 1));
			// height row: [-] value [+]
			UHorizontalBox* HR = HBox();
			for (int32 Sgn = 0; Sgn < 2; ++Sgn)
			{
				UButton* B = Button(Sgn ? 11 : 10, 0, Row);
				UBorder* Back = Panel(Row, Rim, 6.f, FMargin(12, 2, 12, 3));
				Back->SetContent(Label(Sgn ? TEXT("+") : TEXT("−"), 18, Gold, true));
				B->SetContent(Back);
				if (Sgn == 0) { HR->AddChildToHorizontalBox(B)->SetVerticalAlignment(VAlign_Center); continue; }
				SelValue = Text(16, Ink, true);
				SelValue->SetJustification(ETextJustify::Center);
				HR->AddChildToHorizontalBox(Sized(SelValue, 200, 0))->SetVerticalAlignment(VAlign_Center);
				HR->AddChildToHorizontalBox(B)->SetVerticalAlignment(VAlign_Center);
				HR->AddChildToHorizontalBox(Gap(8, 1));
				HR->AddChildToHorizontalBox(KeyCap(TEXT("[ ]"), 11))->SetVerticalAlignment(VAlign_Center);
			}
			SelHeightRow = HR;
			H->AddChildToHorizontalBox(HR)->SetVerticalAlignment(VAlign_Center);
			// valve row: auto / open / closed
			UHorizontalBox* VR = HBox();
			const TCHAR* Modes[] = { TEXT("自动"), TEXT("常开"), TEXT("常闭") };
			for (int32 I = 0; I < 3; ++I)
			{
				UButton* B = Button(12, I, Row);
				UBorder* Back = Panel(Row, FLinearColor::Transparent, 6.f, FMargin(12, 3, 12, 4));
				Back->SetContent(Label(Modes[I], 15, Ink, true));
				B->SetContent(Back);
				SelModeBacks.Add(Back);
				VR->AddChildToHorizontalBox(B)->SetPadding(FMargin(0, 0, 4, 0));
			}
			VR->AddChildToHorizontalBox(Gap(4, 1));
			VR->AddChildToHorizontalBox(KeyCap(TEXT("M"), 11))->SetVerticalAlignment(VAlign_Center);
			SelValveRow = VR;
			H->AddChildToHorizontalBox(VR)->SetVerticalAlignment(VAlign_Center);
			H->AddChildToHorizontalBox(Gap(22, 1));
			for (int32 K = 0; K < 2; ++K)
			{
				UButton* B = Button(K ? 14 : 13, 0, Row);
				UBorder* Back = Panel(Row, FLinearColor::Transparent, 6.f, FMargin(10, 3, 10, 4));
				UHorizontalBox* BH = HBox();
				BH->AddChildToHorizontalBox(Label(K ? TEXT("取消选中") : TEXT("拆除"), 14, K ? Ink : Bad))->SetVerticalAlignment(VAlign_Center);
				BH->AddChildToHorizontalBox(Gap(6, 1));
				BH->AddChildToHorizontalBox(KeyCap(K ? TEXT("Esc") : TEXT("Del"), 10))->SetVerticalAlignment(VAlign_Center);
				Back->SetContent(BH);
				B->SetContent(Back);
				H->AddChildToHorizontalBox(B)->SetPadding(FMargin(0, 0, 4, 0));
			}
			SelPanel->SetContent(H);
			Put(SelPanel, FVector2D(0.5f, 1), FVector2D(0.5f, 1), FVector2D(0, -62));
		}
		// ---------------- toast (under the timeline) ----------------
		{
			ToastPanel = Panel(Fill, Gold, 10.f, FMargin(18, 10, 18, 12));
			UVerticalBox* V = VBox();
			ToastTitle = Text(17, Gold, true);
			ToastBody = Text(15, Ink, false, true, 560.f);
			V->AddChildToVerticalBox(ToastTitle);
			V->AddChildToVerticalBox(Gap(1, 4));
			V->AddChildToVerticalBox(Sized(ToastBody, 570, 0));
			ToastPanel->SetContent(V);
			Put(ToastPanel, FVector2D(0.5f, 0), FVector2D(0.5f, 0), FVector2D(0, 150));
		}
		// ---------------- inspector & legend ----------------
		{
			Inspector = Panel(FLinearColor(0.02f, 0.015f, 0.01f, 0.86f), Rim, 8.f, FMargin(10, 6, 12, 8));
			InspectorText = Text(13, Ink);
			Inspector->SetContent(InspectorText);
			Put(Inspector, FVector2D(0, 0), FVector2D(0, 0), FVector2D(0, 0));

			Legend = Panel(FillSoft, Rim, 8.f, FMargin(12, 8, 12, 8));
			UVerticalBox* V = VBox();
			LegendTitle = Text(14, Gold, true);
			V->AddChildToVerticalBox(LegendTitle);
			UHorizontalBox* Cells = HBox();
			for (int32 I = 0; I < 12; ++I)
			{
				UBorder* C = Panel(FLinearColor::White, FLinearColor::Transparent, 0.f, FMargin(0));
				LegendCells.Add(C);
				Cells->AddChildToHorizontalBox(Sized(C, 18, 12));
			}
			V->AddChildToVerticalBox(Cells)->SetPadding(FMargin(0, 4, 0, 2));
			UHorizontalBox* Lh = HBox();
			LegendLo = Text(12, Dim); LegendHi = Text(12, Dim);
			Lh->AddChildToHorizontalBox(Sized(LegendLo, 108, 0));
			LegendHi->SetJustification(ETextJustify::Right);
			Lh->AddChildToHorizontalBox(Sized(LegendHi, 108, 0));
			V->AddChildToVerticalBox(Lh);
			Legend->SetContent(V);
			Put(Legend, FVector2D(1, 1), FVector2D(1, 1), FVector2D(-18, -66));
		}
		// ---------------- intro ----------------
		{
			Intro = Panel(FLinearColor(0.025f, 0.017f, 0.010f, 0.95f), Gold, 14.f, FMargin(34, 26, 34, 26));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("河谷水利"), 34, Gold, true));
			V->AddChildToVerticalBox(Label(TEXT("《海狸浮生记》（Timberborn）网格水系统交互复现"), 17, Ink));
			V->AddChildToVerticalBox(Gap(1, 14));
			const TCHAR* Lines[] = {
				TEXT("每个格子是一根水柱，相邻格之间用“虚拟管道”交换水量：水头差加速流量，0.999 阻尼保留惯性，出流超过存水时按比例缩小。"),
				TEXT("恶水不是另一种流体，而是附在水上的污染度：顺着水流按体积混合，只在近乎静止的水面之间扩散。"),
				TEXT("季节循环：温和季 4 天，之后是干旱或恶水潮 2 天。恶水潮期间水源污染度从 50% 升到 100%。"),
				TEXT("目标：撑过 3 个危险季，水库污染度保持在 5% 以下，农田作物存活率不低于 80%。"),
				TEXT("右侧“原理实验室”可以逐项切换原作做法与改进方案；V 切换水深、流速、污染度、饱和度、湿度、土壤污染视图。") };
			for (const TCHAR* L : Lines)
			{
				UHorizontalBox* H = HBox();
				H->AddChildToHorizontalBox(Label(TEXT("•"), 16, Gold))->SetPadding(FMargin(0, 0, 8, 0));
				UTextBlock* T = Text(16, Ink, false, true, 640.f);
				T->SetText(FText::FromString(L));
				H->AddChildToHorizontalBox(Sized(T, 650, 0));
				V->AddChildToVerticalBox(H)->SetPadding(FMargin(0, 0, 0, 8));
			}
			V->AddChildToVerticalBox(Gap(1, 10));
			UButton* B = Button(8, 0, RowSel);
			UBorder* Back = Panel(RowSel, Gold, 8.f, FMargin(22, 8, 22, 9));
			UHorizontalBox* H = HBox();
			H->AddChildToHorizontalBox(Label(TEXT("开始"), 18, Ink, true))->SetVerticalAlignment(VAlign_Center);
			H->AddChildToHorizontalBox(Gap(10, 1));
			H->AddChildToHorizontalBox(KeyCap(TEXT("Enter"), 12))->SetVerticalAlignment(VAlign_Center);
			Back->SetContent(H);
			B->SetContent(Back);
			V->AddChildToVerticalBox(B)->SetHorizontalAlignment(HAlign_Left);
			Intro->SetContent(V);
			Put(Intro, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(0, 0));
		}
	}
	return Super::RebuildWidget();
}

void UGWHud::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	AGWPlayerController* P = PC();
	AGWWorld* W = P ? P->GetWater() : nullptr;
	if (!W) return;
	auto Set = [](UTextBlock* T, const FString& S) { if (T) T->SetText(FText::FromString(S)); };
	const bool bShowGame = P->bUiVisible && !P->bIntro;
	for (UWidget* G : GameplayWidgets) G->SetVisibility(bShowGame ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	LabPanel->SetVisibility(bShowGame && P->bLabOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Intro->SetVisibility(P->bIntro ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	// objective
	const float RC = W->ReservoirContamination(), CS = W->CropSurvival();
	Set(ObjHazards, FString::Printf(TEXT("撑过危险季　%d / 3"), FMath::Min(W->HazardsSurvived, 3)));
	Set(ObjReservoir, FString::Printf(TEXT("水库污染度　%.1f%%（上限 5%%）"), RC * 100.f));
	ObjReservoir->SetColorAndOpacity(FSlateColor(RC <= 0.05f ? Good : Bad));
	Set(ObjCrops, FString::Printf(TEXT("作物存活率　%.0f%%（至少 80%%）"), CS * 100.f));
	ObjCrops->SetColorAndOpacity(FSlateColor(CS >= 0.8f ? Good : Bad));
	if (W->HazardsSurvived >= 3) Set(ObjHazards, RC <= 0.05f && CS >= 0.8f ? TEXT("目标达成！可以继续实验") : TEXT("已撑过 3 个危险季"));

	// season
	const TCHAR* PhaseName = W->Phase == EGWPhase::Temperate ? TEXT("温和季") : W->Phase == EGWPhase::Drought ? TEXT("干旱") : TEXT("恶水潮");
	Set(SeasonTitle, FString::Printf(TEXT("第 %d 周期 · %s"), W->Cycle, PhaseName));
	SeasonTitle->SetColorAndOpacity(FSlateColor(W->Phase == EGWPhase::Temperate ? Ink : W->Phase == EGWPhase::Drought ? DayDrought * 1.3f : Bad));
	FString Sub = FString::Printf(TEXT("本季还剩 %.1f 天"), FMath::Max(W->PhaseDaysLeft(), 0.f));
	if (W->Phase == EGWPhase::Badtide) Sub += FString::Printf(TEXT(" · 水源污染度 %.0f%%"), W->SourceContamination * 100.f);
	if (W->SourceModifier < 0.999f) Sub += FString::Printf(TEXT(" · 水源强度 %.0f%%"), W->SourceModifier * 100.f);
	Set(SeasonSub, Sub);
	const float CycleDay = (W->Phase == EGWPhase::Temperate ? 0.f : AGWWorld::TemperateDays) + W->DayOfPhase();
	for (int32 D = 0; D < DayCells.Num(); ++D)
	{
		const int32 InCycle = D % 6;
		const bool bThis = D < 6;
		FLinearColor C = DayTemperate;
		if (InCycle >= 4)
		{
			const EGWPhase Hz = bThis ? (W->Phase == EGWPhase::Temperate ? W->NextHazard : W->Phase) : (W->Phase == EGWPhase::Temperate ? (W->NextHazard == EGWPhase::Drought ? EGWPhase::Badtide : EGWPhase::Drought) : W->NextHazard);
			C = Hz == EGWPhase::Drought ? DayDrought : DayBadtide;
		}
		const float Elapsed = bThis ? FMath::Clamp(CycleDay - InCycle, 0.f, 1.f) : 0.f;
		const bool bNow = bThis && CycleDay >= InCycle && CycleDay < InCycle + 1;
		C = C * (bThis ? FMath::Lerp(1.0f, 0.45f, Elapsed >= 1.f ? 1.f : 0.f) : 0.7f);
		C.A = 1.f;
		DayCells[D]->SetBrush(Rounded(C, 3.f, bNow ? Gold : FLinearColor::Transparent, bNow ? 2.f : 0.f));
	}
	for (int32 I = 0; I < SpeedBacks.Num(); ++I) SpeedBacks[I]->SetBrush(Rounded(I == W->SpeedIndex ? RowSel : Row, 5.f));
	Set(HazardText, FString::Printf(TEXT("下个危险季：%s  B"), W->NextHazard == EGWPhase::Drought ? TEXT("干旱") : TEXT("恶水潮")));

	// tools
	for (int32 I = 0; I < ToolBacks.Num(); ++I) ToolBacks[I]->SetBrush(Rounded(ToolOrder[I] == P->Tool ? RowSel : Row, 6.f, ToolOrder[I] == P->Tool ? Gold : FLinearColor::Transparent, 1.f));
	FString Help = AGWPlayerController::ToolHelp(P->Tool);
	if (P->Tool == EGWTool::Valve || P->Tool == EGWTool::Dam) { static const TCHAR* D[] = { TEXT("东"), TEXT("南"), TEXT("西"), TEXT("北") }; Help += FString::Printf(TEXT("\n朝向：%s（Z 旋转）"), D[P->Dir]); }
	if (P->Tool == EGWTool::Floodgate) Help += FString::Printf(TEXT("\n新闸高度：%.2f 格"), W->DefaultGateH);
	Set(ToolHelp, Help);
	Set(ToolStatus, P->Tool != EGWTool::None && P->HoverCell != INDEX_NONE && !P->bHoverValid ? P->HoverWhy : FString());

	// lab
	for (int32 I = 0; I < PrincipleValues.Num(); ++I)
	{
		Set(PrincipleValues[I], P->PrincipleValue(I));
		PrinciplePills[I]->SetBrush(Rounded(P->PrincipleIsOriginal(I) ? FLinearColor(0.34f, 0.22f, 0.06f, 1.f) : FLinearColor(0.06f, 0.36f, 0.38f, 1.f), 9.f));
	}
	for (int32 I = 0; I < OverlayBacks.Num(); ++I) OverlayBacks[I]->SetBrush(Rounded(I == (int32)W->Overlay ? RowSel : Row, 5.f));
	for (int32 I = 0; I < ParamValues.Num(); ++I) Set(ParamValues[I], P->ParamValue(I));

	// ledger
	const FWaterSim& S = W->Sim;
	const double Err = S.WaterError(), CErr = S.ContamError();
	const FString V[] = {
		FString::Printf(TEXT("%.1f"), S.TotalWater()), FString::Printf(TEXT("%.1f"), S.L.Injected), FString::Printf(TEXT("%.1f"), S.L.Drained),
		FString::Printf(TEXT("%.1f"), S.L.Evaporated), FString::Printf(TEXT("%.1f"), S.L.Pumped), FString::Printf(TEXT("%+.3f"), Err),
		FString::Printf(TEXT("%.2f"), S.TotalContaminant()), FString::Printf(TEXT("%+.3f"), CErr),
		FString::Printf(TEXT("%.1f · %.1f%%"), W->ReservoirVolume(), RC * 100.f), FString::Printf(TEXT("%.2f / %.1f"), W->PumpedBadTotal(), W->PumpedTotal()) };
	for (int32 I = 0; I < LedgerValues.Num(); ++I) Set(LedgerValues[I], V[I]);
	LedgerValues[5]->SetColorAndOpacity(FSlateColor(FMath::Abs(Err) > 0.5 ? Bad : Good));
	LedgerValues[7]->SetColorAndOpacity(FSlateColor(FMath::Abs(CErr) > 0.05 ? Bad : Good));

	// selection panel
	const int32 SelI = W->SelectedPrimary != INDEX_NONE ? W->FindStructure(W->SelectedPrimary) : INDEX_NONE;
	SelPanel->SetVisibility(bShowGame && SelI != INDEX_NONE ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (SelI != INDEX_NONE)
	{
		const FGWStructure& St = W->Structures[SelI];
		Set(SelTitle, FString::Printf(TEXT("已选中：%s × %d"), *AGWPlayerController::ToolName(St.Type), W->Selected.Num()));
		const bool bHeight = St.Type == EGWTool::Floodgate || St.Type == EGWTool::Levee;
		SelHeightRow->SetVisibility(bHeight ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		SelValveRow->SetVisibility(St.Type == EGWTool::Valve ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (St.Type == EGWTool::Floodgate) Set(SelValue, FString::Printf(TEXT("闸板高度 %.2f 格"), St.GateH));
		if (St.Type == EGWTool::Levee) Set(SelValue, FString::Printf(TEXT("堤高 %d 格"), St.LeveeH));
		const int32 Mode = P->SelectedValveMode();
		for (int32 I = 0; I < SelModeBacks.Num(); ++I) SelModeBacks[I]->SetBrush(Rounded(I == Mode ? RowSel : Row, 6.f, I == Mode ? Gold : FLinearColor::Transparent, 1.f));
	}

	// toast (7 s, fading over the last second)
	const float Age = GetWorld()->GetRealTimeSeconds() - P->ToastTime;
	const float A = FMath::Clamp(8.f - Age, 0.f, 1.f);
	ToastPanel->SetVisibility(bShowGame && A > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	ToastPanel->SetRenderOpacity(A);
	Set(ToastTitle, P->ToastTitle); Set(ToastBody, P->ToastBody);

	// inspector next to the cursor
	const int32 C = P->HoverCell;
	if (bShowGame && P->bInspector && C != INDEX_NONE)
	{
		static const TCHAR* Dn[] = { TEXT("东"), TEXT("南"), TEXT("西"), TEXT("北") };
		FString T = FString::Printf(TEXT("格子 (%d, %d)　地面 %.0f 层\n水深 %.3f　水头 %.2f\n"), C % AGWWorld::GW, C / AGWWorld::GW, S.Ground[C], S.Depth[C], S.Head(C));
		T += TEXT("出流 ");
		for (int32 K = 0; K < 4; ++K) T += FString::Printf(TEXT("%s%.2f "), Dn[K], S.Flow[C * 4 + K]);
		T += FString::Printf(TEXT("\n污染度 %.1f%%　饱和度 %d\n湿度 %.1f　土壤污染 %.0f%%"), S.Conc[C] * 100.f, S.Sat[C], S.Moist[C], S.SoilC[C] * 100.f);
		const int32 Si = W->FindStructure(C);
		if (Si != INDEX_NONE)
		{
			const FGWStructure& St = W->Structures[Si];
			T += TEXT("\n建筑：") + AGWPlayerController::ToolName(St.Type);
			if (St.Type == EGWTool::Floodgate) T += FString::Printf(TEXT("　闸高 %.2f（[ ] 调整）"), St.GateH);
			if (St.Type == EGWTool::Valve) T += St.bAuto ? (St.bClosed ? TEXT("　自动·已关闭") : TEXT("　自动·开启")) : (St.bClosed ? TEXT("　常闭") : TEXT("　常开"));
			if (St.Type == EGWTool::Pump) T += FString::Printf(TEXT("　已抽 %.1f（恶水 %.2f）"), St.Pumped, St.PumpedBad);
		}
		Set(InspectorText, T);
		const FVector2D M = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
		if (UCanvasPanelSlot* CS2 = Cast<UCanvasPanelSlot>(Inspector->Slot)) CS2->SetPosition(M + FVector2D(22, 18));
		Inspector->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else Inspector->SetVisibility(ESlateVisibility::Collapsed);

	// legend for the active debug view
	const int32 Ov = (int32)W->Overlay;
	Legend->SetVisibility(bShowGame && Ov != 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (Ov != 0)
	{
		const FLinearColor* R = Ov == 1 ? RampDepth : Ov == 2 ? RampSpeed : Ov == 3 ? RampContam : Ov == 4 ? RampSat : Ov == 5 ? RampMoist : RampSoil;
		static const TCHAR* Lo[] = { TEXT(""), TEXT("0 格"), TEXT("静止"), TEXT("清水 0%"), TEXT("1"), TEXT("干燥 0"), TEXT("0%") };
		static const TCHAR* Hi[] = { TEXT(""), TEXT("3 格"), TEXT("2.5 格/秒"), TEXT("恶水 100%"), TEXT("8"), TEXT("16 格"), TEXT("100%") };
		Set(LegendTitle, TEXT("视图：") + AGWPlayerController::OverlayName(Ov));
		for (int32 I = 0; I < LegendCells.Num(); ++I) LegendCells[I]->SetBrushColor(Ramp4(R, (I + 0.5f) / LegendCells.Num()));
		Set(LegendLo, Lo[Ov]); Set(LegendHi, Hi[Ov]);
	}
}
