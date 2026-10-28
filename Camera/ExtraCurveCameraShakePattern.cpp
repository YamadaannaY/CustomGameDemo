#include "ExtraCurveCameraShakePattern.h"
#include "Curves/CurveVector.h"

void UExtraCurveCameraShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	// State.Update 推进时间并返回当前淡入/淡出权重
	const float BlendWeight = State.Update(Params.DeltaTime);

	SampleCurve(State.GetElapsedTime(), OutResult);

	OutResult.ApplyScale(BlendWeight);
}

void UExtraCurveCameraShakePattern::ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	const float BlendWeight = State.Scrub(Params.AbsoluteTime);

	SampleCurve(Params.AbsoluteTime, OutResult);

	OutResult.ApplyScale(BlendWeight);
}

void UExtraCurveCameraShakePattern::SampleCurve(float Time, FCameraShakePatternUpdateResult& OutResult) const
{
	if (!ShakeCurve)
	{
		return;
	}

	float CurveTime = Time;
	if (bNormalizeCurveTime && State.HasDuration())
	{
		const float ShakeDuration = State.GetDuration();
		if (ShakeDuration <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		CurveTime = FMath::Clamp(Time / ShakeDuration, 0.f, 1.f);
	}
	
	const TArray<FRichCurveEditInfo> Curves = ShakeCurve->GetCurves();
	auto EvalChannel = [&Curves, CurveTime](int32 Index) -> float
	{
		return Curves.IsValidIndex(Index) && Curves[Index].CurveToEdit
			? Curves[Index].CurveToEdit->Eval(CurveTime, 0.f)
			: 0.f;
	};

	const FVector CurveValue(EvalChannel(0), EvalChannel(1), EvalChannel(2));
	const FVector Offset = CurveValue * CurveScale;

	if (bApplyToRotation)
	{
		// 曲线 X/Y/Z 分别对应绕本地 X/Y/Z 轴旋转：Roll / Pitch / Yaw
		OutResult.Rotation += FRotator(Offset.Y, Offset.Z, Offset.X);
	}
	else
	{
		OutResult.Location += Offset;
	}
}
