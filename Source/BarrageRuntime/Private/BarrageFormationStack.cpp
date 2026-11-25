#include "BarrageFormationStack.h"
#include "Math/RandomStream.h"

namespace
{
    constexpr int32 MaxBlocks = 64;
    constexpr int32 MaxGenerators = 32;
    constexpr double Tau = 2.0 * UE_PI;

    bool Fail(FString* Error, const FString& Message)
    {
        if (Error) *Error = Message;
        return false;
    }

    bool IsValidGeometry(const FBarragePatternSettings& S)
    {
        return S.Count > 0 && S.Rows > 0 && S.Columns > 0 && S.Depth > 0 &&
            S.Arms > 0 && S.Petals >= 1 && S.Petals <= 32 &&
            FMath::IsFinite(S.Radius) && S.Radius >= 0 &&
            FMath::IsFinite(S.InnerRadius) && S.InnerRadius >= 0 &&
            FMath::IsFinite(S.Width) && S.Width >= 0 &&
            FMath::IsFinite(S.Height) && S.Height >= 0 &&
            FMath::IsFinite(S.Length) && S.Length >= 0 &&
            FMath::IsFinite(S.AngleOffsetDegrees) &&
            FMath::IsFinite(S.AngleSpanDegrees) &&
            FMath::IsFinite(S.Turns) &&
            FMath::IsFinite(S.Amplitude) &&
            FMath::IsFinite(S.Frequency) &&
            FMath::IsFinite(S.GapFraction) && S.GapFraction >= 0 && S.GapFraction <= 1 &&
            FMath::IsFinite(S.ConeAngleDegrees);
    }

    void UpdateDirection(FBarragePatternEmission& Shot, const FBarragePatternSettings& Settings,
        bool bHasTarget, const FVector& Target)
    {
        if (Settings.bAimAtTarget && bHasTarget && !Shot.bOrbit && Shot.AnchorIndex == INDEX_NONE)
            Shot.LocalDirection = (Target - Shot.LocalPosition).GetSafeNormal(UE_SMALL_NUMBER, Shot.LocalDirection);
        else if (Settings.Direction == EBarrageDirectionMode::RadialOutward ||
                 Settings.Direction == EBarrageDirectionMode::RadialInward ||
                 Settings.Direction == EBarrageDirectionMode::TowardCenter)
        {
            const FVector Radial = Shot.LocalPosition.GetSafeNormal(UE_SMALL_NUMBER, Shot.LocalDirection);
            Shot.LocalDirection = Settings.Direction == EBarrageDirectionMode::RadialOutward ? Radial : -Radial;
        }
        Shot.LocalDirection.Normalize();
    }

    bool InsideGap(const FBarragePatternEmission& Shot, int32 Index, int32 Num,
        const FBarragePatternSettings& S)
    {
        double Phase;
        if (FMath::Square(Shot.LocalPosition.X) + FMath::Square(Shot.LocalPosition.Y) > 1.e-10)
        {
            Phase = FMath::Fmod((FMath::Atan2(Shot.LocalPosition.Y, Shot.LocalPosition.X) -
                FMath::DegreesToRadians(double(S.AngleOffsetDegrees))) / Tau, 1.0);
            if (Phase < 0.0) Phase += 1.0;
        }
        else Phase = double(Index) / FMath::Max(Num, 1);
        return FMath::Min(Phase, 1.0 - Phase) < double(S.GapFraction) * 0.5;
    }
}

bool FBarrageFormationStackCompiler::Validate(const FBarragePatternLayer& Layer, FString* Error)
{
    if (!Layer.LocalTransform.IsValid()) return Fail(Error,TEXT("Formation / Transform: invalid transform."));
    if (Layer.FormationBlocks.IsEmpty() || Layer.FormationBlocks.Num() > MaxBlocks)
        return Fail(Error,TEXT("Formation / Blocks: add at least one generator; maximum 64 blocks."));
    if (!Layer.Settings.bComposable || Layer.Settings.Count < 1 || Layer.Settings.Count > 50000)
        return Fail(Error,TEXT("Formation / Count: enter 1 to 50000 samples, using composable behavior settings."));
    int32 Generators = 0;
    bool bHaveGenerator = false;
    TSet<FGuid> Ids;
    for (int32 I=0; I<Layer.FormationBlocks.Num(); ++I)
    {
        const FBarrageFormationBlock& B = Layer.FormationBlocks[I];
        if (!B.BlockId.IsValid() || Ids.Contains(B.BlockId))
            return Fail(Error,FString::Printf(TEXT("Formation / Block %d: missing or duplicated stable ID."),I+1));
        Ids.Add(B.BlockId);
        if (!B.bEnabled) continue;
        if (!IsValidGeometry(B.Geometry) || !FMath::IsFinite(B.ModifierAngleDegrees) || !FMath::IsFinite(B.ModifierDistanceCm) ||
            B.ModifierJitterCm.ContainsNaN() || B.ModifierJitterCm.X < 0.0 || B.ModifierJitterCm.Y < 0.0 || B.ModifierJitterCm.Z < 0.0 ||
            B.ModifierJitterCm.GetAbsMax() > 100000.0 ||
            B.ModifierOffset.ContainsNaN() || B.ModifierScale.ContainsNaN())
            return Fail(Error,FString::Printf(TEXT("Formation / Block %d: invalid geometric parameters."),I+1));
        if (B.Kind == EBarrageFormationBlockKind::Generator)
        {
            bHaveGenerator = true;
            if (++Generators > MaxGenerators || B.Weight < 1 || B.Weight > 100000)
                return Fail(Error,FString::Printf(TEXT("Formation / Block %d: invalid generator count or weight."),I+1));
        }
        else if (!bHaveGenerator)
            return Fail(Error,FString::Printf(TEXT("Formation / Block %d: a modifier must follow an enabled generator."),I+1));
        else if (uint8(B.Kind) > uint8(EBarrageFormationBlockKind::PositionJitter))
            return Fail(Error,FString::Printf(TEXT("Formation / Block %d: unknown modifier."),I+1));
    }
    if (Generators == 0 || Generators > Layer.Settings.Count)
        return Fail(Error,TEXT("Formation / Count: at least one enabled generator and one sample per generator are required."));
    return true;
}

bool FBarrageFormationStackCompiler::ConvertToBlocks(FBarragePatternLayer& Layer)
{
    if (!Layer.FormationBlocks.IsEmpty()) return Validate(Layer);
    FBarragePatternLayer Original = Layer;
    Layer.Settings = FBarragePatternLibrary::MakeComposable(Layer.Settings);
    // Count now represents the exact generator-sample budget rather than the
    // per-arm or ignored Grid count. Preserve the pre-modifier geometry size.
    FBarragePatternSettings Plain = Layer.Settings;
    Plain.Modifier = EBarrageFormationModifier::None;
    Plain.Emission = EBarrageEmissionMode::Instant;
    Plain.bMultipleDirections = false;
    const double Count = FBarragePatternLibrary::EstimateEmissionCount(Plain);
    if (Count < 1.0 || Count > 50000.0)
    {
        Layer = MoveTemp(Original);
        return false;
    }
    Layer.Settings.Count = int32(Count);
    Layer.FormationBlocks.Reset();
    FBarrageFormationBlock Generator;
    Generator.BlockId = FGuid::NewGuid();
    Generator.Kind = EBarrageFormationBlockKind::Generator;
    Generator.Geometry = Plain;
    Layer.FormationBlocks.Add(MoveTemp(Generator));
    if (Layer.Settings.Modifier != EBarrageFormationModifier::None)
    {
        FBarrageFormationBlock Modifier;
        Modifier.BlockId = FGuid::NewGuid();
        Modifier.Geometry = Layer.Settings;
        switch (Layer.Settings.Modifier)
        {
        case EBarrageFormationModifier::Wave: Modifier.Kind = EBarrageFormationBlockKind::Wave; break;
        case EBarrageFormationModifier::Alternating: Modifier.Kind = EBarrageFormationBlockKind::Alternating; break;
        case EBarrageFormationModifier::Mirror: Modifier.Kind = EBarrageFormationBlockKind::Mirror; break;
        case EBarrageFormationModifier::AngularGap: Modifier.Kind = EBarrageFormationBlockKind::AngularGap; break;
        default: break;
        }
        Layer.FormationBlocks.Add(MoveTemp(Modifier));
    }
    // The old preset's modifier no longer operates in parallel with the stack.
    Layer.Settings.bFormationBlockEditor = true;
    if (!Validate(Layer))
    {
        // Never convert an attack to a currently unsupported Block combination.
        // Undo remains available for successful conversions in the editor.
        Layer = MoveTemp(Original);
        return false;
    }
    return true;
}

bool FBarrageFormationStackCompiler::Generate(const FBarragePatternLayer& Layer,
    TArray<FBarragePatternEmission>& Out, int32 Budget, bool bHasTarget, FVector Target, FString* Error)
{
    Out.Reset();
    if (!Validate(Layer,Error)) return false;
    if (Target.ContainsNaN() || Budget < 1 || Budget > 1000000)
        return Fail(Error,TEXT("Formation: invalid target or emission budget."));

    const FBarragePatternSettings& Settings = Layer.Settings;
    int32 GeneratorCount=0;
    int64 TotalWeight=0;
    for (const FBarrageFormationBlock& B : Layer.FormationBlocks)
        if (B.bEnabled && B.Kind==EBarrageFormationBlockKind::Generator)
        {++GeneratorCount; TotalWeight+=B.Weight;}
    if (TotalWeight <= 0) return Fail(Error,TEXT("Formation: no generator weight."));
    const int32 Remaining = Settings.Count - GeneratorCount;
    int64 WeightBefore=0;
    int32 GeneratorOrdinal=0;
    TArray<FBarragePatternEmission> Samples;
    Samples.Reserve(Settings.Count);
    for (int32 BlockIndex=0; BlockIndex<Layer.FormationBlocks.Num(); ++BlockIndex)
    {
        const FBarrageFormationBlock& B = Layer.FormationBlocks[BlockIndex];
        if (!B.bEnabled) continue;
        if (B.Kind == EBarrageFormationBlockKind::Generator)
        {
            const int32 Amount = 1 + int32((int64(Remaining)*(WeightBefore+B.Weight))/TotalWeight -
                (int64(Remaining)*WeightBefore)/TotalWeight);
            WeightBefore += B.Weight;
            ++GeneratorOrdinal;
            FBarragePatternSettings G = Settings;
            const FBarragePatternSettings& Geometry = B.Geometry;
            G.Formation=Geometry.Formation;
            G.Modifier=EBarrageFormationModifier::None;
            G.Emission=EBarrageEmissionMode::Instant;
            G.bMultipleDirections=false;
            G.bNaturalGroups=false;
            // Spokes and Helix generate Count samples *per arm*. Allocate
            // the per-arm count before selecting the exact requested budget.
            G.Count=(G.Formation==EBarrageFormation::Spokes || G.Formation==EBarrageFormation::Helix)
                ? FMath::DivideAndRoundUp(Amount,FMath::Max(1,G.Arms)) : Amount;
            G.Rows=Geometry.Rows; G.Columns=Geometry.Columns; G.Depth=Geometry.Depth;
            G.Arms=Geometry.Arms; G.Radius=Geometry.Radius; G.InnerRadius=Geometry.InnerRadius;
            G.Width=Geometry.Width; G.Height=Geometry.Height; G.Length=Geometry.Length;
            G.AngleOffsetDegrees=Geometry.AngleOffsetDegrees;
            G.AngleSpanDegrees=Geometry.AngleSpanDegrees; G.Turns=Geometry.Turns;
            G.Petals=Geometry.Petals; G.ConeAngleDegrees=Geometry.ConeAngleDegrees;
            G.RandomSeed=Geometry.RandomSeed; G.bVolume=Geometry.bVolume;
            TArray<FBarragePatternEmission> Generated;
            // Some geometric primitives (Grid, Spokes, Helix) produce natural
            // sample counts. Select a stable subset to honor the ONE Count budget.
            if (!FBarragePatternLibrary::Generate(G,Generated,50000,bHasTarget,Target) ||
                Generated.Num()<Amount || Generated.Num()>50000)
                return Fail(Error,FString::Printf(TEXT("Formation / Generator %d: geometry supplies fewer than %d distinct samples or is invalid."),GeneratorOrdinal,Amount));
            for (int32 I=0;I<Amount;++I)
            {
                const int32 Pick=int32(int64(I)*Generated.Num()/Amount);
                Samples.Add(Generated[Pick]);
            }
        }
        else
        {
            const FBarragePatternSettings& M=B.Geometry;
            TArray<FBarragePatternEmission> Modified;
            Modified.Reserve(FMath::Min(Budget+1, Samples.Num()*2));
            FRandomStream JitterStream(B.ModifierSeed);
            for (int32 I=0; I<Samples.Num(); ++I)
            {
                FBarragePatternEmission Shot=Samples[I];
                switch (B.Kind)
                {
                case EBarrageFormationBlockKind::Wave:
                    Shot.LocalPosition.Z += M.Amplitude*FMath::Sin(Tau*double(M.Frequency)*
                        (Samples.Num()>1?double(I)/(Samples.Num()-1):0.5));
                    UpdateDirection(Shot,Settings,bHasTarget,Target);
                    Modified.Add(Shot);
                    break;
                case EBarrageFormationBlockKind::Alternating:
                    if (I%2==0) Modified.Add(Shot);
                    break;
                case EBarrageFormationBlockKind::Mirror:
                    for (int32 Sign : {-1,1})
                    {
                        FBarragePatternEmission Copy=Shot;
                        Copy.LocalPosition.X=Sign*(Shot.LocalPosition.X+M.Length*0.5);
                        Copy.LocalDirection.X*=Sign;
                        if (Settings.bAimAtTarget && bHasTarget && !Copy.bOrbit)
                            Copy.LocalDirection=(Target-Copy.LocalPosition).GetSafeNormal(UE_SMALL_NUMBER,Copy.LocalDirection);
                        Modified.Add(Copy);
                    }
                    break;
                case EBarrageFormationBlockKind::AngularGap:
                    if (!InsideGap(Shot,I,Samples.Num(),M)) Modified.Add(Shot);
                    break;
                case EBarrageFormationBlockKind::Translate:
                    Shot.LocalPosition+=B.ModifierOffset;
                    UpdateDirection(Shot,Settings,bHasTarget,Target);
                    Modified.Add(Shot);
                    break;
                case EBarrageFormationBlockKind::Rotate:
                {
                    const FQuat Rotate(FVector::UpVector,FMath::DegreesToRadians(double(B.ModifierAngleDegrees)));
                    Shot.LocalPosition=Rotate.RotateVector(Shot.LocalPosition);
                    Shot.LocalDirection=Rotate.RotateVector(Shot.LocalDirection);
                    UpdateDirection(Shot,Settings,bHasTarget,Target);
                    Modified.Add(Shot);
                    break;
                }
                case EBarrageFormationBlockKind::Scale:
                    Shot.LocalPosition*=B.ModifierScale;
                    UpdateDirection(Shot,Settings,bHasTarget,Target);
                    Modified.Add(Shot);
                    break;
                case EBarrageFormationBlockKind::RadialOffset:
                    Shot.LocalPosition += Shot.LocalPosition.GetSafeNormal(UE_SMALL_NUMBER, Shot.LocalDirection) * B.ModifierDistanceCm;
                    UpdateDirection(Shot,Settings,bHasTarget,Target);
                    Modified.Add(Shot);
                    break;
                case EBarrageFormationBlockKind::DirectionRotate:
                    Shot.LocalDirection=FQuat(FVector::UpVector,FMath::DegreesToRadians(double(B.ModifierAngleDegrees))).RotateVector(Shot.LocalDirection);
                    // Heading overrides are intentional, even for radial-direction generators.
                    Shot.LocalDirection.Normalize();
                    Modified.Add(Shot);
                    break;
                case EBarrageFormationBlockKind::PositionJitter:
                    Shot.LocalPosition += FVector(
                        JitterStream.FRandRange(-B.ModifierJitterCm.X,B.ModifierJitterCm.X),
                        JitterStream.FRandRange(-B.ModifierJitterCm.Y,B.ModifierJitterCm.Y),
                        JitterStream.FRandRange(-B.ModifierJitterCm.Z,B.ModifierJitterCm.Z));
                    UpdateDirection(Shot,Settings,bHasTarget,Target);
                    Modified.Add(Shot);
                    break;
                default: return Fail(Error,TEXT("Formation: unknown block kind."));
                }
                if (Modified.Num()>Budget) return Fail(Error,TEXT("Formation: modifier output exceeds emission budget."));
            }
            for(const FBarragePatternEmission& Sample:Modified)
                if(Sample.LocalPosition.ContainsNaN() || Sample.LocalDirection.ContainsNaN())
                    return Fail(Error,TEXT("Formation: modifier produced invalid coordinates."));
            Samples=MoveTemp(Modified);
        }
        if (Samples.Num()>Budget)
            return Fail(Error,TEXT("Formation: intermediate sample count exceeds emission budget."));
    }
    if (Samples.IsEmpty())
        return Fail(Error,TEXT("Formation: all samples were filtered; adjust the modifiers or Count."));
    if (Samples.Num() > Budget) return Fail(Error,TEXT("Formation: samples exceed budget."));
    Out = MoveTemp(Samples);
    // Timing is always owned by the ordered Emission Stack.
    for (FBarragePatternEmission& Shot : Out) Shot.TimeSeconds = 0.0f;
    return true;
}

double FBarrageFormationStackCompiler::EstimateCount(const FBarragePatternLayer& Layer, int32 Budget)
{
    TArray<FBarragePatternEmission> Shots;
    return Generate(Layer,Shots,Budget) ? double(Shots.Num()) : -1.0;
}
