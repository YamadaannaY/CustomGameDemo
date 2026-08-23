#include "FCombatCameraDebuggerCategory.h"

#if WITH_GAMEPLAY_DEBUGGER

#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "ExtractGameCharacter/Camera/UCombatCameraComponent.h"

FCombatCameraDebuggerCategory::FCombatCameraDebuggerCategory()
{
	// 无 DebugActor 选中时也显示（针对玩家自身相机，而非 AI 目标）。
	bShowOnlyWithDebugActor = false;
}

TSharedRef<FGameplayDebuggerCategory> FCombatCameraDebuggerCategory::MakeInstance()
{
	return MakeShareable(new FCombatCameraDebuggerCategory());
}

void FCombatCameraDebuggerCategory::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	// 单机下相机状态在 DrawData 里直接读取，无需数据包复制。
}

void FCombatCameraDebuggerCategory::DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext)
{
	if (!OwnerPC || !OwnerPC->GetPawn())
	{
		return;
	}

	APawn* Pawn = OwnerPC->GetPawn();
	UWorld* World = OwnerPC->GetWorld();

	UCombatCameraComponent* CamComp = Pawn->FindComponentByClass<UCombatCameraComponent>();
	if (!CamComp)
	{
		CanvasContext.Print(FColor::Red, TEXT("No UCombatCameraComponent on pawn"));
		return;
	}

	// ── 3D 可视化：相机实际位置 + 视线方向 ──
	if (World)
	{
		if (UCameraComponent* Cam = CamComp->GetFollowCamera())
		{
			const FVector CamLoc = Cam->GetComponentLocation();
			const FVector CamFwd = Cam->GetForwardVector();
			DrawDebugSphere(World, CamLoc, 12.f, 12, FColor::Green, false, -1.f, 0, 1.f);
			DrawDebugLine(World, CamLoc, CamLoc + CamFwd * 300.f, FColor::Green, false, -1.f, 0, 1.f);
		}

		// SpringArm 基准点（角色背后），用于直观对比偏移幅度。
		if (USpringArmComponent* Boom = CamComp->GetCameraBoom())
		{
			const FVector BaseLoc = Boom->GetComponentLocation();
			DrawDebugSphere(World, BaseLoc, 6.f, 8, FColor::Yellow, false, -1.f, 0, 1.f);
		}
	}

	// ── 数值面板 ──
	CanvasContext.Print(FColor::Green, TEXT("--- Combat Camera ---"));

	const FVector LocOffset = CamComp->GetCurrentLocationOffset();
	const FRotator RotOffset = CamComp->GetCurrentRotationOffset();

	CanvasContext.Print(
		FColor::White,
		FString::Printf(TEXT("LocOffset  (%.0f, %.0f, %.0f)"), LocOffset.X, LocOffset.Y, LocOffset.Z));

	CanvasContext.Print(
		FColor::White,
		FString::Printf(TEXT("RotOffset  (P=%.1f, Y=%.1f, R=%.1f)"), RotOffset.Pitch, RotOffset.Yaw, RotOffset.Roll));

	CanvasContext.Print(
		FColor::White,
		FString::Printf(TEXT("ArmLength  %.0f   FOV  %.1f"), CamComp->GetCurrentArmLength(), CamComp->GetCurrentFOV()));

	CanvasContext.Print(
		FColor::White,
		FString::Printf(TEXT("ActiveReq  %s"), CamComp->HasActiveRequest() ? TEXT("YES") : TEXT("NO")));

	if (const FCombatCameraRequest* Req = CamComp->GetActiveRequest())
	{
		CanvasContext.Print(
			FColor::Yellow,
			FString::Printf(TEXT("  target Loc (%.0f, %.0f, %.0f)  Arm %.0f  FOV %.1f  BlendIn %.2f  BlendOut %.2f"),
				Req->LocationOffset.X, Req->LocationOffset.Y, Req->LocationOffset.Z,
				Req->ArmLength, Req->FOV, Req->BlendInTime, Req->BlendOutTime));
	}
}

#endif // WITH_GAMEPLAY_DEBUGGER
