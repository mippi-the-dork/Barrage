#include "BarrageEmissionStack.h"
#include "BarrageFormationStack.h"
#include "Math/RandomStream.h"

namespace
{
    bool Fail(FString* Error,const FString& Message)
    { if (Error) *Error=Message; return false; }
}

bool FBarrageEmissionStackCompiler::Validate(const FBarragePatternLayer& L, FString* Error)
{
    if (L.EmissionBlocks.Num()<1 || L.EmissionBlocks.Num()>64)
        return Fail(Error,TEXT("Emission: add one Instant source and at most 63 operators."));
    int32 Sources=0;
    TSet<FGuid> Ids;
    for (int32 I=0;I<L.EmissionBlocks.Num();++I)
    {
        const FBarrageEmissionBlock& B=L.EmissionBlocks[I];
        if (!B.BlockId.IsValid() || Ids.Contains(B.BlockId))
            return Fail(Error,TEXT("Emission: duplicate or invalid Block ID."));
        Ids.Add(B.BlockId);
        if (!B.bEnabled) continue;
        if (uint8(B.Kind)>uint8(EBarrageEmissionBlockKind::DirectionSweep))
            return Fail(Error,TEXT("Emission: unknown operator."));
        if (!FMath::IsFinite(B.IntervalSeconds) || B.IntervalSeconds<0.0f ||
            !FMath::IsFinite(B.RotationPerRepeatDegrees) ||
            B.EveryNthShot<1 || B.EveryNthShot>1024 || !FMath::IsFinite(B.DirectionStepDegrees) ||
            B.GroupSize<1 || B.RepeatCount<1 || B.RepeatCount>10000 || B.ArmCount<1 || B.ArmCount>128)
            return Fail(Error,FString::Printf(TEXT("Emission / Block %d: invalid parameters."),I+1));
        if (B.Kind==EBarrageEmissionBlockKind::Instant)
        {
            if (I!=0 || ++Sources>1)
                return Fail(Error,TEXT("Emission: Instant source must be the first and only source Block."));
        }
    }
    if (Sources!=1) return Fail(Error,TEXT("Emission: one enabled Instant source is required."));
    return true;
}

bool FBarrageEmissionStackCompiler::ConvertToBlocks(FBarragePatternLayer& L)
{
    if (!L.EmissionBlocks.IsEmpty()) return Validate(L);
    if (!L.Settings.bComposable || L.Settings.Emission==EBarrageEmissionMode::ParentThenFormation) return false;
    const FBarragePatternLayer Original=L;
    L.EmissionBlocks.Reset();
    FBarrageEmissionBlock Start;
    Start.BlockId=FGuid::NewGuid();
    L.EmissionBlocks.Add(Start);
    // Direction arms expand every sample before the schedule operators. This
    // preserves the built-in multi-arm recipe without a separate runtime path.
    if (L.Settings.bMultipleDirections && L.Settings.Direction == EBarrageDirectionMode::Rotating)
    {
        FBarrageEmissionBlock Arms;
        Arms.BlockId = FGuid::NewGuid();
        Arms.Kind = EBarrageEmissionBlockKind::ArmDirections;
        Arms.ArmCount = L.Settings.DirectionArms;
        L.EmissionBlocks.Add(Arms);
    }
    FBarrageEmissionBlock Next;
    Next.BlockId=FGuid::NewGuid();
    Next.IntervalSeconds=L.Settings.IntervalSeconds;
    switch (L.Settings.Emission)
    {
    case EBarrageEmissionMode::Sequential:
        Next.Kind=EBarrageEmissionBlockKind::Stagger;
        L.EmissionBlocks.Add(Next); break;
    case EBarrageEmissionMode::Grouped:
        Next.Kind=EBarrageEmissionBlockKind::Group;
        Next.GroupSize=L.Settings.bNaturalGroups
            ? (L.Settings.Formation==EBarrageFormation::Grid
                ? FMath::Max(1,L.Settings.Columns / (L.Settings.Modifier==EBarrageFormationModifier::Alternating?2:1))
                : (L.Settings.bMultipleDirections ? L.Settings.DirectionArms : L.Settings.GroupSize))
            : L.Settings.GroupSize;
        L.EmissionBlocks.Add(Next); break;
    case EBarrageEmissionMode::Waves:
        Next.Kind=EBarrageEmissionBlockKind::Repeat;
        Next.RepeatCount=L.Settings.Waves;
        Next.RotationPerRepeatDegrees=L.Settings.bRotateWaves
            ? 360.0f*L.Settings.WaveTurns/FMath::Max(1,L.Settings.Waves) : 0.0f;
        L.EmissionBlocks.Add(Next); break;
    default: break;
    }
    L.Settings.bEmissionBlockEditor=true;
    if (!Validate(L)) { L=Original; return false; }
    return true;
}

bool FBarrageEmissionStackCompiler::Apply(const FBarragePatternLayer& L,
    TArray<FBarragePatternEmission>& InOut, int32 Budget, FString* Error)
{
    if (!Validate(L,Error)) return false;
    if (Budget<1 || Budget>1000000 || InOut.IsEmpty() || InOut.Num()>Budget)
        return Fail(Error,TEXT("Emission: no source samples or invalid budget."));
    for (const FBarrageEmissionBlock& B : L.EmissionBlocks)
    {
        if (!B.bEnabled) continue;
        const int32 Num=InOut.Num();
        switch (B.Kind)
        {
        case EBarrageEmissionBlockKind::Instant:
            // Source Block is a zero-time schedule identity. Only once, first.
            break;
        case EBarrageEmissionBlockKind::Stagger:
        case EBarrageEmissionBlockKind::Group:
            for (int32 I=0;I<Num;++I)
            {
                const double Extra=(B.Kind==EBarrageEmissionBlockKind::Stagger ? double(I)
                    : double(I/B.GroupSize))*B.IntervalSeconds;
                const double Time=double(InOut[I].TimeSeconds)+Extra;
                if (!FMath::IsFinite(Time) || Time>86400.0)
                    return Fail(Error,TEXT("Emission: staggered time exceeds allowed playback duration."));
                InOut[I].TimeSeconds=float(Time);
            }
            break;
        case EBarrageEmissionBlockKind::Repeat:
        {
            if (int64(Num)*B.RepeatCount > Budget)
                return Fail(Error,TEXT("Emission: Repeat exceeds shot budget."));
            const TArray<FBarragePatternEmission> Input=InOut;
            InOut.Reserve(Num*B.RepeatCount);
            for (int32 Wave=1;Wave<B.RepeatCount;++Wave)
            {
                const FQuat Rotation(FVector::UpVector,FMath::DegreesToRadians(double(B.RotationPerRepeatDegrees)*Wave));
                for (const FBarragePatternEmission& Source : Input)
                {
                    FBarragePatternEmission Shot=Source;
                    Shot.LocalPosition=Rotation.RotateVector(Shot.LocalPosition);
                    Shot.LocalDirection=Rotation.RotateVector(Shot.LocalDirection);
                    const double Time=double(Shot.TimeSeconds)+double(Wave)*B.IntervalSeconds;
                    if (!FMath::IsFinite(Time) || Time>86400.0)
                        return Fail(Error,TEXT("Emission: repeat time exceeds allowed playback duration."));
                    Shot.TimeSeconds=float(Time);
                    InOut.Add(MoveTemp(Shot));
                }
            }
            break;
        }
        case EBarrageEmissionBlockKind::Shuffle:
        {
            FRandomStream Stream(B.RandomSeed);
            for (int32 I=Num-1;I>0;--I) InOut.Swap(I,Stream.RandRange(0,I));
            break;
        }
        case EBarrageEmissionBlockKind::PingPong:
        {
            if (int64(Num)*2 > Budget) return Fail(Error,TEXT("Emission: Ping Pong exceeds shot budget."));
            const TArray<FBarragePatternEmission> Input=InOut;
            InOut.Reserve(Num*2);
            for(int32 I=Num-1;I>=0;--I)
            {
                FBarragePatternEmission Shot=Input[I];
                const double Time=double(Shot.TimeSeconds)+B.IntervalSeconds;
                if (!FMath::IsFinite(Time) || Time>86400.0) return Fail(Error,TEXT("Emission: Ping Pong time exceeds duration."));
                Shot.TimeSeconds=float(Time);
                InOut.Add(MoveTemp(Shot));
            }
            break;
        }
        case EBarrageEmissionBlockKind::Quantize:
            if (B.IntervalSeconds<=0.0f) return Fail(Error,TEXT("Emission: Quantize Time requires positive interval."));
            for(FBarragePatternEmission& Shot:InOut)
            {
                const double Time=FMath::CeilToDouble(double(Shot.TimeSeconds)/double(B.IntervalSeconds))*B.IntervalSeconds;
                if(!FMath::IsFinite(Time) || Time>86400.0) return Fail(Error,TEXT("Emission: Quantized time invalid."));
                Shot.TimeSeconds=float(Time);
            }
            break;
        case EBarrageEmissionBlockKind::TimeJitter:
        {
            FRandomStream Stream(B.RandomSeed);
            for(FBarragePatternEmission& Shot:InOut)
            {
                const double Time=FMath::Max(0.0,double(Shot.TimeSeconds)+double(Stream.FRandRange(-B.IntervalSeconds,B.IntervalSeconds)));
                if (!FMath::IsFinite(Time) || Time>86400.0) return Fail(Error,TEXT("Emission: jittered time invalid."));
                Shot.TimeSeconds=float(Time);
            }
            break;
        }
        case EBarrageEmissionBlockKind::EveryNth:
        {
            TArray<FBarragePatternEmission> Kept;
            Kept.Reserve(FMath::DivideAndRoundUp(Num,B.EveryNthShot));
            for(int32 I=0; I<Num; I+=B.EveryNthShot) Kept.Add(InOut[I]);
            InOut=MoveTemp(Kept);
            break;
        }
        case EBarrageEmissionBlockKind::DirectionSweep:
            for(int32 I=0; I<Num; ++I)
            {
                const FQuat Rotation(FVector::UpVector,FMath::DegreesToRadians(double(B.DirectionStepDegrees)*I));
                InOut[I].LocalDirection=Rotation.RotateVector(InOut[I].LocalDirection);
                if(InOut[I].LocalDirection.ContainsNaN()) return Fail(Error,TEXT("Emission: direction sweep produced invalid heading."));
            }
            break;
        case EBarrageEmissionBlockKind::Reverse:
            for (int32 I=0; I<Num/2; ++I) InOut.Swap(I,Num-I-1);
            break;
        case EBarrageEmissionBlockKind::ArmDirections:
        {
            if (int64(Num)*B.ArmCount > Budget)
                return Fail(Error,TEXT("Emission: Direction Arms exceed shot budget."));
            TArray<FBarragePatternEmission> Expanded;
            Expanded.Reserve(Num*B.ArmCount);
            for (const FBarragePatternEmission& Source : InOut)
                for (int32 Arm=0;Arm<B.ArmCount;++Arm)
                {
                    FBarragePatternEmission Shot=Source;
                    const FQuat Rotation(FVector::UpVector,2.0*UE_PI*double(Arm)/B.ArmCount);
                    Shot.LocalDirection=Rotation.RotateVector(Source.LocalDirection);
                    Expanded.Add(MoveTemp(Shot));
                }
            InOut=MoveTemp(Expanded);
            break;
        }
        default: return Fail(Error,TEXT("Emission: invalid Block kind."));
        }
        if (InOut.Num()>Budget) return Fail(Error,TEXT("Emission: excessive intermediate output."));
    }
    return true;
}

double FBarrageEmissionStackCompiler::EstimateCount(const FBarragePatternLayer& L,int32 Budget)
{
    FBarragePatternLayer Source=L;
    TArray<FBarragePatternEmission> Shots;
    const bool bGenerated=FBarrageFormationStackCompiler::Generate(Source,Shots,Budget);
    if (!bGenerated || !Apply(L,Shots,Budget)) return -1.0;
    return double(Shots.Num());
}
