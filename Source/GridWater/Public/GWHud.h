#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GWHud.generated.h"

class UTextBlock;
class UBorder;
class UButton;
class UVerticalBox;
class UHorizontalBox;
class UCanvasPanel;
class UCanvasPanelSlot;
class AGWPlayerController;

/** Click target for one button; forwards to the controller. */
UCLASS()
class UGWClick : public UObject
{
	GENERATED_BODY()
public:
	int32 Kind = 0;		// 0 tool, 1 principle, 2 param -, 3 param +, 4 overlay, 5 speed, 6 next phase, 7 hazard toggle, 8 start, 9 lab
	int32 Index = 0;
	TWeakObjectPtr<AGWPlayerController> Owner;
	UFUNCTION() void OnClicked();
};

/**
 * The whole HUD, built in code: objective card, season timeline, build palette, a "principle lab" with
 * the switches and debug views from the report, a water ledger, an inspector that follows the cursor,
 * a legend for the active debug view, toasts that explain each switch, and the intro card.
 */
UCLASS()
class GRIDWATER_API UGWHud : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	AGWPlayerController* PC() const;
	UTextBlock* Text(int32 Size, FLinearColor Color, bool bBold = false, bool bWrap = false, float WrapAt = 0.f);
	UBorder* Panel(FLinearColor Fill, FLinearColor Rim, float Radius, FMargin Pad);
	UBorder* KeyCap(const FString& Key, int32 Size = 12);
	UButton* Button(int32 Kind, int32 Index, FLinearColor Fill);
	UCanvasPanelSlot* Put(UWidget* W, FVector2D Anchor, FVector2D Align, FVector2D Pos);

	UPROPERTY() TArray<TObjectPtr<UGWClick>> Clicks;
	UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;

	// objective
	UPROPERTY() TObjectPtr<UTextBlock> ObjHazards;
	UPROPERTY() TObjectPtr<UTextBlock> ObjReservoir;
	UPROPERTY() TObjectPtr<UTextBlock> ObjCrops;
	// season
	UPROPERTY() TObjectPtr<UTextBlock> SeasonTitle;
	UPROPERTY() TObjectPtr<UTextBlock> SeasonSub;
	UPROPERTY() TArray<TObjectPtr<UBorder>> DayCells;
	UPROPERTY() TArray<TObjectPtr<UBorder>> SpeedBacks;
	UPROPERTY() TObjectPtr<UTextBlock> HazardText;
	// tools
	UPROPERTY() TArray<TObjectPtr<UBorder>> ToolBacks;
	UPROPERTY() TObjectPtr<UTextBlock> ToolHelp;
	UPROPERTY() TObjectPtr<UTextBlock> ToolStatus;
	// lab
	UPROPERTY() TObjectPtr<UBorder> LabPanel;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> PrincipleValues;
	UPROPERTY() TArray<TObjectPtr<UBorder>> PrinciplePills;
	UPROPERTY() TArray<TObjectPtr<UBorder>> OverlayBacks;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ParamValues;
	// ledger
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> LedgerValues;
	// toast, inspector, legend, intro, hints
	UPROPERTY() TObjectPtr<UBorder> ToastPanel;
	UPROPERTY() TObjectPtr<UTextBlock> ToastTitle;
	UPROPERTY() TObjectPtr<UTextBlock> ToastBody;
	UPROPERTY() TObjectPtr<UBorder> Inspector;
	UPROPERTY() TObjectPtr<UTextBlock> InspectorText;
	UPROPERTY() TObjectPtr<UBorder> Legend;
	UPROPERTY() TObjectPtr<UTextBlock> LegendTitle;
	UPROPERTY() TArray<TObjectPtr<UBorder>> LegendCells;
	UPROPERTY() TObjectPtr<UTextBlock> LegendLo;
	UPROPERTY() TObjectPtr<UTextBlock> LegendHi;
	UPROPERTY() TObjectPtr<UBorder> Intro;
	// selection panel
	UPROPERTY() TObjectPtr<UBorder> SelPanel;
	UPROPERTY() TObjectPtr<UTextBlock> SelTitle;
	UPROPERTY() TObjectPtr<UTextBlock> SelValue;
	UPROPERTY() TObjectPtr<UWidget> SelHeightRow;
	UPROPERTY() TObjectPtr<UWidget> SelValveRow;
	UPROPERTY() TArray<TObjectPtr<UBorder>> SelModeBacks;
	UPROPERTY() TArray<TObjectPtr<UWidget>> GameplayWidgets;
};
