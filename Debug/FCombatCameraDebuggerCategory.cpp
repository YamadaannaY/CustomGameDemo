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
	const FRotator ArmRotOffset = CamComp->GetCurrentArmRotationOffset();

	CanvasContext.Print(
		FColor::White,
		FString::Printf(TEXT("CamOffset  (%.0f, %.0f, %.0f)"), LocOffset.X, LocOffset.Y, LocOffset.Z));

	CanvasContext.Print(
		FColor::White,
		FString::Printf(TEXT("ArmRot     (P=%.1f, Y=%.1f, R=%.1f)"), ArmRotOffset.Pitch, ArmRotOffset.Yaw, ArmRotOffset.Roll));

	CanvasContext.Print(
		FColor::White,
		FString::Printf(TEXT("ArmLength  %.0f   FOV  %.1f"), CamComp->GetCurrentArmLength(), CamComp->GetCurrentFOV()));

	CanvasContext.Print(
		FColor::White,
		FString::Printf(TEXT("ActiveReq  %s"), CamComp->HasActiveRequest() ? TEXT("YES") : TEXT("NO")));

	if (const FCombatCameraRequest* Req = CamComp->GetActiveRequest())
	{
		// 只打印本镜头实际接管的项，便于确认「是否修改」有没有漏勾
		CanvasContext.Print(
			FColor::Yellow,
			FString::Printf(TEXT("  Arm %.0f%s  FOV %.1f%s  CamY %.0f%s  CamZ %.0f%s  ArmRot(P%.0f Y%.0f R%.0f)%s"),
				Req->ArmLength, Req->bModifyArmLength ? TEXT("*") : TEXT("-"),
				Req->FOV, Req->bModifyFOV ? TEXT("*") : TEXT("-"),
				Req->CameraOffsetY, Req->bModifyCameraOffsetY ? TEXT("*") : TEXT("-"),
				Req->CameraOffsetZ, Req->bModifyCameraOffsetZ ? TEXT("*") : TEXT("-"),
				Req->ArmRotation.Pitch, Req->ArmRotation.Yaw, Req->ArmRotation.Roll,
				Req->bModifyArmRotation ? TEXT("*") : TEXT("-")));

		CanvasContext.Print(
			FColor::Yellow,
			FString::Printf(TEXT("  FacingBasis %s  LockLook %s  BlendIn %.2f  BlendOut %.2f  Interrupt %.2f"),
				Req->bUseCharacterFacingBasis ? TEXT("Y") : TEXT("N"),
				Req->bLockLookInput ? TEXT("Y") : TEXT("N"),
				Req->BlendInTime, Req->BlendOutTime, Req->InterruptedBlendOutTime));
	}
}

#endif // WITH_GAMEPLAY_DEBUGGER
