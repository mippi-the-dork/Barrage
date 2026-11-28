#include "BarragePatternPreviewModel.h"
#include "BarrageAimMath.h"
#include "BarrageAssets.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarrageStackPrograms.h"

FVector FBarragePreviewTarget::PositionAt(double Seconds) const
{
    if (!bEnabled || !bPingPong || HalfRangeCm <= 0.0f || SpeedCmPerSecond <= 0.0f)
        return Center;
    const double Range = static_cast<double>(HalfRangeCm);
    const double Period = 4.0 * Range / SpeedCmPerSecond;
    const double Phase = FMath::Fmod(FMath::Max(0.0, Seconds), Period) / Period;
    // Start at the left endpoint and traverse the same arch in both directions.
    const double X = Phase < 0.5 ? -Range + 4.0 * Range * Phase : 3.0 * Range - 4.0 * Range * Phase;
    const double Z = ArcHeightCm * (1.0 - FMath::Square(X / Range));
    return Center + FVector(X, 0.0, Z);
}

FVector FBarragePreviewTarget::VelocityAt(double Seconds) const
{
    if (!bEnabled || !bPingPong || HalfRangeCm <= 0.0f || SpeedCmPerSecond <= 0.0f)
        return FVector::ZeroVector;
    const double Period = 4.0 * HalfRangeCm / SpeedCmPerSecond;
    const double Phase = FMath::Fmod(FMath::Max(0.0, Seconds), Period) / Period;
    const double VX = Phase < 0.5 ? double(SpeedCmPerSecond) : -double(SpeedCmPerSecond);
    const double Offset = PositionAt(Seconds).X - Center.X;
    return FVector(VX, 0.0, -2.0 * ArcHeightCm * Offset * VX / FMath::Square(double(HalfRangeCm)));
}

bool FBarragePreviewTarget::ContainsPoint(const FVector& WorldPoint, double Seconds, double ShotRadiusCm) const
{
    if (!bEnabled) return false;
    const FVector Local = WorldPoint - PositionAt(Seconds);
    const double Radius = FMath::Max(1.0, double(CapsuleRadiusCm)) + FMath::Max(0.0, ShotRadiusCm);
    const double Segment = FMath::Max(0.0, double(CapsuleHalfHeightCm) - FMath::Max(1.0, double(CapsuleRadiusCm)));
    const FVector Closest(0.0, 0.0, FMath::Clamp(Local.Z, -Segment, Segment));
    return FVector::DistSquared(Local, Closest) <= Radius * Radius;
}

bool FBarragePreviewTarget::SegmentHits(const FVector& From, const FVector& To, double FromTime, double ToTime, double ShotRadiusCm) const
{
    if (!bEnabled) return false;
    // Temporal subdivision captures crossings between frames without World queries.
    // More subdivisions are needed for fast shots; bound work per projectile.
    const double Chord = FVector::Distance(From, To);
    const int32 Steps = FMath::Clamp(FMath::CeilToInt(Chord / FMath::Max(4.0, double(CapsuleRadiusCm) * 0.5)), 1, 32);
    for (int32 I = 0; I <= Steps; ++I)
    {
        const double Alpha = double(I) / Steps;
        if (ContainsPoint(FMath::Lerp(From, To, Alpha), FMath::Lerp(FromTime, ToTime, Alpha), ShotRadiusCm))
            return true;
    }
    return false;
}

namespace
{
    FLinearColor LayerPalette(int32 Index)
    {
        static const FLinearColor Colors[] = {
            FLinearColor(1.0f, 0.67f, 0.27f), FLinearColor(0.48f, 0.94f, 0.58f),
            FLinearColor(0.92f, 0.48f, 0.95f), FLinearColor(1.0f, 0.45f, 0.50f),
            FLinearColor(0.52f, 0.69f, 1.0f), FLinearColor(0.88f, 0.91f, 0.42f)
        };
        return Colors[Index % UE_ARRAY_COUNT(Colors)];
    }
}

FLinearColor FBarragePatternPreviewModel::GetLayerColor(const UBarragePattern* Pattern, int32 LayerIndex)
{
    if (!Pattern) return FLinearColor::White;
    if (LayerIndex == INDEX_NONE) return FLinearColor::White;
    if (!Pattern->Layers.IsValidIndex(LayerIndex)) return FLinearColor::White;
    const FBarragePatternLayer& Layer = Pattern->Layers[LayerIndex];
    return Layer.bOverridePreviewColor ? Layer.PreviewColor : LayerPalette(LayerIndex);
}

void FBarragePatternPreviewModel::Reset()
{
    Points.Reset();
    TrackedGroupPaths.Reset();
    ImpactTimes.Reset();
    Duration = 0.0;
    FlightDuration = 0.0;
    bValid = false;
    bTruncated = false;
    bHasUnsupportedGravity = false;
    EmissionBounds = FBox(EForceInit::ForceInit);
    FlightBounds = FBox(EForceInit::ForceInit);
    Message = TEXT("Add an enabled layer and assign a Projectile Profile.");
}

bool FBarragePatternPreviewModel::Rebuild(const UBarragePattern* Pattern, int32 MaxEmissions)
{
    Reset();
    if (!::IsValid(Pattern) || MaxEmissions < 1 || MaxEmissions > 50000)
    {
        Message = TEXT("Invalid Pattern or preview budget.");
        return false;
    }

    FBarrageActivePlayback Playback;
    bool bAim = false;
    for (const FBarragePatternLayer& Layer : Pattern->Layers)
    {
        bAim |= Layer.Settings.bAimAtTarget;
        bAim |= Layer.MotionBlocks.ContainsByPredicate([](const FBarrageMotionBlock& B)
            { return B.Kind == EBarrageMotionBlockKind::Tracker; });
    }
    const FVector PreviewTarget = Target.bEnabled ? Target.PositionAt(0.0) : FVector(500.0, 0.0, 0.0);
    if (!FBarragePatternCompiler::CompilePattern(Pattern, FTransform::Identity,
        bAim, PreviewTarget, Playback, MaxEmissions))
    {
        // Surface specific Block errors before falling back to a general
        // dependency/profile/budget message. Preview and production still share
        // one compiler; this extra pass runs ONLY after compilation fails.
        for (const FBarragePatternLayer& Layer : Pattern->Layers)
        {
            if (!Layer.bEnabled) continue;
            FString StackError;
            FBarrageMotionProgram ValidMotion;
            if (!FBarrageStackPrograms::ValidateActivation(Layer,&StackError) ||
                !FBarrageStackPrograms::CompileMotion(Layer,ValidMotion,&StackError))
            {
                Message=FString::Printf(TEXT("Layer '%s': %s"),*Layer.Name,*StackError);
                return false;
            }
            FBarragePatternLayer Input=Layer;
            Input.Settings.Emission=EBarrageEmissionMode::Instant;
            TArray<FBarragePatternEmission> DiagnosticShots;
            FString Diagnostic;
            const bool bGeometryOk=FBarrageFormationStackCompiler::Generate(
                Input,DiagnosticShots,MaxEmissions,bAim,PreviewTarget,&Diagnostic);
            if (!bGeometryOk)
            {
                Message=FString::Printf(TEXT("Layer '%s': %s"),*Layer.Name,
                    Diagnostic.IsEmpty()?TEXT("Invalid formation parameters or sample budget."):*Diagnostic);
                return false;
            }
            if (!FBarrageEmissionStackCompiler::Apply(Layer,DiagnosticShots,MaxEmissions,&Diagnostic))
            {
                Message=FString::Printf(TEXT("Layer '%s': %s"),*Layer.Name,*Diagnostic);
                return false;
            }
        }
        Message = TEXT("Cannot generate preview: check enabled layers, profiles, activation dependencies/cycles, or total emission budget.");
        return false;
    }

    // The production compiler owns formation and modifier geometry. Do not
    // reimplement Wave, Alternating, Mirror or Angular Gap in the preview.
    // Expand deterministic projectile events against the same compiled shots.
    // This is analytic only: world collision and explicit destruction remain PIE-only.
    Points.Reserve(FMath::Min(MaxEmissions, Playback.Shots.Num() + 64));
    TArray<int32> InvocationIds;
    InvocationIds.Reserve(Points.Max());
    auto AddPoint = [&](const FBarrageScheduledShot& Shot, double AbsoluteTime,
                        const FVector& Origin, int32 AnchorIndex) -> int32
    {
        FBarragePreviewPoint& P = Points.AddDefaulted_GetRef();
        P.Position = Shot.WorldPosition + Origin;
        P.RelativeAnchorOffset = Shot.WorldPosition;
        P.Direction = Shot.WorldDirection;
        if (Shot.AimStrength > 0.0f && AnchorIndex == INDEX_NONE)
        {
            const FVector TargetAtSpawn = Target.bEnabled ? Target.PositionAt(AbsoluteTime) : Shot.TrackerTargetPosition;
            const FVector Toward = (TargetAtSpawn - P.Position).GetSafeNormal(UE_SMALL_NUMBER, Shot.AuthoredWorldDirection);
            P.Direction = BarrageAimMath::BlendDirections(Shot.AuthoredWorldDirection, Toward, Shot.AimStrength);
        }
        P.FireTime = AbsoluteTime;
        P.OrbitCenter = Shot.OrbitCenter + Origin;
        P.bGroupOrbit = Shot.bGroupOrbit;
        P.GroupIdentity = Shot.GroupIdentity;
        P.GroupTravelVelocity = Shot.GroupTravelVelocity;
        P.GroupTurnRateDegrees = Shot.GroupTurnRateDegrees;
        P.GroupReleaseSeconds = Shot.GroupReleaseSeconds;
        P.GroupClockOffsetSeconds = Shot.GroupClockOffsetSeconds;
        P.OrbitAxis = Shot.OrbitAxis;
        P.OrbitDegreesPerSecond = Shot.OrbitDegreesPerSecond;
        P.OrbitReleaseSeconds = Shot.OrbitReleaseAgeSeconds;
        P.StationaryReleaseAgeSeconds = Shot.StationaryReleaseAgeSeconds;
        P.MotionProgram = Shot.MotionProgram;
        P.TrackerTargetPosition = Shot.TrackerTargetPosition;
        P.bHasTrackerTarget = Shot.bHasTrackerTarget;
        P.MovementTransitionAgeSeconds = Shot.MovementTransitionAgeSeconds;
        P.MovementTransitionTo = Shot.MovementTransitionTo;
        P.TransitionTurnRateDegrees = Shot.TransitionTurnRateDegrees;
        P.bOrbit = Shot.bOrbit;
        P.bCurved = Shot.bCurved;
        P.LayerIndex = Shot.LayerIndex;
        P.AnchorIndex = AnchorIndex;
        P.bAnchored = AnchorIndex != INDEX_NONE;
        if (Shot.Profile)
        {
            P.Speed = Shot.bStationary ? 0.0f : Shot.Profile->Speed;
            P.Lifetime = Shot.Profile->Lifetime;
            P.CollisionMode = Shot.Profile->CollisionMode;
            P.CollisionRadiusCm = Shot.Profile->CollisionRadius;
            P.ReleaseSpeed = Shot.Profile->Speed;
            P.ReleaseGravityMode = Shot.Profile->GravityMode;
            P.GravityMode = (Shot.bStationary || Shot.bCurved) ? EBarrageGravityMode::None : Shot.Profile->GravityMode;
            P.ConstantAcceleration = Shot.Profile->ConstantAcceleration;
            bHasUnsupportedGravity |= (P.MotionProgram.NumPhases>1 &&
                (P.ReleaseGravityMode==EBarrageGravityMode::ProviderAtSpawn ||
                 P.ReleaseGravityMode==EBarrageGravityMode::ProviderContinuous)) ||
                P.GravityMode == EBarrageGravityMode::ProviderAtSpawn ||
                P.GravityMode == EBarrageGravityMode::ProviderContinuous ||
                (P.StationaryReleaseAgeSeconds > 0.0f &&
                (P.ReleaseGravityMode == EBarrageGravityMode::ProviderAtSpawn ||
                 P.ReleaseGravityMode == EBarrageGravityMode::ProviderContinuous)) ||
                (P.MovementTransitionAgeSeconds > 0.0f && P.MovementTransitionTo == EBarrageTransitionMovement::Profile &&
                 (P.ReleaseGravityMode == EBarrageGravityMode::ProviderAtSpawn ||
                  P.ReleaseGravityMode == EBarrageGravityMode::ProviderContinuous));
        }
        P.ActiveColor = GetLayerColor(Pattern, P.LayerIndex);
        InvocationIds.Add(Shot.InvocationId);
        ImpactTimes.Add(-1.0);
        return Points.Num()-1;
    };

    for (const FBarrageScheduledShot& Shot : Playback.Shots)
        AddPoint(Shot, Shot.FireTime, FVector::ZeroVector, Shot.AnchorIndex);
    // Original schedule anchor indices are already sorted/remapped by the compiler.
    // The authored emission view places parent-relative shots at their parent's
    // initial position; Travel evaluates the parent's moving position instead.
    for (int32 I=0; I<Points.Num(); ++I)
        if (Points[I].bAnchored && Points.IsValidIndex(Points[I].AnchorIndex))
            Points[I].Position += Points[Points[I].AnchorIndex].Position;

    bool bEstimatedEvents=false;
    bool bNeedsPIE=false;
    bool bDistanceApproximationCapped=false;
    int32 NextPreviewEventGroupIdentity = -2;
    const double EventSampleStep = 1.0/60.0;
    const double MaximumEventTravelSeconds = 20.0;
    // Source shots can themselves come from an event. Breadth-first expansion
    // processes dependent chains without recursion and obeys a hard shot budget.
    for (int32 SourceIndex=0; SourceIndex<Points.Num(); ++SourceIndex)
    {
        if (Points.Num() >= MaxEmissions) { bTruncated = true; break; }
        const FBarragePreviewPoint Source = Points[SourceIndex];
        if (Source.Lifetime <= 0.0f || !FMath::IsFinite(Source.Lifetime)) continue;
        // One earliest hit per source; evaluated once and shared by dependent rules.
        if (Target.bEnabled && Source.CollisionMode != EBarrageCollisionMode::Disabled &&
            SourceIndex < 512 && ImpactTimes.IsValidIndex(SourceIndex) && ImpactTimes[SourceIndex] < 0.0)
        {
            const double EndAge = FMath::Min(8.0, double(Source.Lifetime));
            constexpr double HitStep = 1.0 / 60.0;
            FVector Previous = EvaluatePosition(SourceIndex, Source.FireTime);
            if (Target.ContainsPoint(Previous, Source.FireTime, Source.CollisionRadiusCm)) ImpactTimes[SourceIndex] = Source.FireTime;
            else
            {
                for (int32 Step = 1; Step <= FMath::CeilToInt(EndAge / HitStep); ++Step)
                {
                    const double T = Source.FireTime + FMath::Min(EndAge, Step * HitStep);
                    const FVector Current = EvaluatePosition(SourceIndex, T);
                    if (Target.SegmentHits(Previous, Current, T - HitStep, T, Source.CollisionRadiusCm))
                    {
                        ImpactTimes[SourceIndex] = T;
                        break;
                    }
                    Previous = Current;
                }
            }
        }
        for (const FBarrageEventLayer& Rule : Playback.EventLayers)
        {
            if (Rule.InvocationId != InvocationIds[SourceIndex] || Rule.SourceLayerIndex != Source.LayerIndex) continue;
            if (Rule.Activation == EBarrageLayerActivation::OnProjectileDeath ||
                (Rule.Activation == EBarrageLayerActivation::OnProjectileImpact && !Target.bEnabled))
            {
                bNeedsPIE = true;
                continue;
            }
            double EventTime = 0.0;
            FVector EventPosition = FVector::ZeroVector;
            bool bTriggered = false;
            if (Rule.Activation == EBarrageLayerActivation::OnProjectileImpact &&
                ImpactTimes.IsValidIndex(SourceIndex) && ImpactTimes[SourceIndex] >= 0.0)
            {
                EventTime = ImpactTimes[SourceIndex];
                EventPosition = EvaluatePosition(SourceIndex, EventTime);
                bTriggered = true;
            }
            else if (Rule.Activation == EBarrageLayerActivation::OnProjectileExpired)
            {
                EventTime = Source.FireTime + double(Source.Lifetime);
                EventPosition = EvaluatePosition(SourceIndex, EventTime);
                bTriggered = true;
            }
            else if (Rule.Activation == EBarrageLayerActivation::AfterProjectileDistance)
            {
                if (Source.GravityMode == EBarrageGravityMode::ProviderAtSpawn ||
                    Source.GravityMode == EBarrageGravityMode::ProviderContinuous ||
                    (Source.StationaryReleaseAgeSeconds > 0.0f &&
                    (Source.ReleaseGravityMode == EBarrageGravityMode::ProviderAtSpawn ||
                     Source.ReleaseGravityMode == EBarrageGravityMode::ProviderContinuous)))
                {
                    bNeedsPIE = true;
                    continue;
                }
                const double SampleAge = FMath::Min(double(Source.Lifetime),MaximumEventTravelSeconds);
                bDistanceApproximationCapped |= double(Source.Lifetime) > MaximumEventTravelSeconds;
                // Dense straight or curved shots with zero acceleration travel at
                // constant path speed. Solve their distance threshold in O(1)
                // rather than sampling up to 1,200 positions for EACH shot.
                // Quantize to a 60 Hz sample, consistent with the fallback and
                // the default fixed-step World simulation.
                const bool bRelease = Source.StationaryReleaseAgeSeconds > 0.0f;
                const EBarrageGravityMode EffectiveGravity = bRelease ? Source.ReleaseGravityMode : Source.GravityMode;
                const bool bZeroAcceleration = EffectiveGravity == EBarrageGravityMode::None ||
                    (EffectiveGravity == EBarrageGravityMode::Constant && Source.ConstantAcceleration.IsNearlyZero());
                const double Speed = bRelease ? double(Source.ReleaseSpeed) : double(Source.Speed);
                const double MovingStartAge = bRelease ? double(Source.StationaryReleaseAgeSeconds) : 0.0;
                const bool bSimplePath = !Source.bOrbit && !Source.bGroupOrbit && bZeroAcceleration && FMath::IsFinite(Speed);
                if (bSimplePath && Speed > UE_SMALL_NUMBER)
                {
                    const double RequiredAge = MovingStartAge + double(Rule.DistanceCm) / Speed;
                    if (RequiredAge <= SampleAge + 1.0e-8)
                    {
                        const double SampledAge = FMath::CeilToInt(RequiredAge / EventSampleStep - 1.0e-6) * EventSampleStep;
                        if (SampledAge <= SampleAge + 1.0e-8)
                        {
                            EventTime = Source.FireTime + SampledAge;
                            EventPosition = EvaluatePosition(SourceIndex, EventTime);
                            bTriggered = !EventPosition.ContainsNaN();
                        }
                    }
                }
                else if (!bSimplePath || Speed > UE_SMALL_NUMBER)
                {
                    // Orbit and gravity paths may change speed; measure the
                    // cumulative polyline distance, not just net displacement.
                    const int32 Steps = FMath::Max(1,FMath::CeilToInt(SampleAge/EventSampleStep));
                    FVector Previous = EvaluatePosition(SourceIndex,Source.FireTime);
                    double Traveled = 0.0;
                    for (int32 Step=1; Step<=Steps; ++Step)
                    {
                        const double Age = FMath::Min(SampleAge,Step*EventSampleStep);
                        const FVector Current = EvaluatePosition(SourceIndex,Source.FireTime+Age);
                        if (Current.ContainsNaN()) break;
                        Traveled += FVector::Distance(Previous,Current);
                        if (Traveled >= Rule.DistanceCm)
                        {
                            EventTime = Source.FireTime+Age;
                            EventPosition = Current;
                            bTriggered = true;
                            break;
                        }
                        Previous = Current;
                    }
                }
            }
            if (!bTriggered || EventPosition.ContainsNaN()) continue;
            bEstimatedEvents = true;
            // Template anchor indices are local to the event's shot array.
            // Use local-to-global remapping, so nested Parent Then Formation
            // shots still resolve after the preview's final chronological sort.
            const int32 EventGroupIdentity = NextPreviewEventGroupIdentity--;
            TArray<int32> TemplateMap;
            TemplateMap.Reserve(Rule.TemplateShots.Num());
            for (const FBarrageScheduledShot& Template : Rule.TemplateShots)
            {
                if (Points.Num() >= MaxEmissions) { bTruncated = true; break; }
                const int32 Anchor = TemplateMap.IsValidIndex(Template.AnchorIndex)
                    ? TemplateMap[Template.AnchorIndex] : INDEX_NONE;
                const int32 Added = AddPoint(Template,
                    EventTime + double(Rule.StartDelaySeconds) + Template.FireTime,
                    Anchor == INDEX_NONE ? EventPosition : Points[Anchor].Position, Anchor);
                if (Template.bGroupOrbit) Points[Added].GroupIdentity = EventGroupIdentity;
                TemplateMap.Add(Added);
            }
        }
    }
    // Event chains arrive in dependency order, not necessarily time order.
    // CountThrough and shot-stepping require chronological order, and dynamic
    // anchor links must retain their identity through the permutation.
    if (bEstimatedEvents)
    {
        TArray<int32> Order;
        Order.Reserve(Points.Num());
        for (int32 I=0;I<Points.Num();++I) Order.Add(I);
        Order.Sort([this](int32 A,int32 B)
        {
            if (Points[A].FireTime != Points[B].FireTime) return Points[A].FireTime < Points[B].FireTime;
            return A<B;
        });
        TArray<int32> Remap;
        Remap.SetNum(Points.Num());
        TArray<FBarragePreviewPoint> Chronological;
        TArray<double> ChronologicalImpacts;
        Chronological.Reserve(Points.Num());
        ChronologicalImpacts.Reserve(Points.Num());
        for (int32 I=0;I<Order.Num();++I)
        {
            Remap[Order[I]]=I;
            Chronological.Add(MoveTemp(Points[Order[I]]));
            ChronologicalImpacts.Add(ImpactTimes[Order[I]]);
        }
        for (FBarragePreviewPoint& Point : Chronological)
            if (Point.bAnchored && Remap.IsValidIndex(Point.AnchorIndex))
                Point.AnchorIndex=Remap[Point.AnchorIndex];
        Points=MoveTemp(Chronological);
        ImpactTimes=MoveTemp(ChronologicalImpacts);
    }
    Duration = Points.IsEmpty() ? 0.0 : Points.Last().FireTime;
    FlightDuration = Duration;
    for (const FBarragePreviewPoint& P : Points)
    {
        // Capped preview keeps slider useful even for extremely long-lived bullets.
        if (FMath::IsFinite(P.Lifetime) && P.Lifetime > 0)
            FlightDuration = FMath::Max(FlightDuration, P.FireTime + FMath::Min(8.0, static_cast<double>(P.Lifetime)));
    }
    // Stable camera framing for the full authored volume. Dynamic recentering every
    // scrub frame makes a moving bullet appear stationary, so fit is compiled once.
    for (int32 I = 0; I < Points.Num(); ++I)
    {
        const FBarragePreviewPoint& P = Points[I];
        EmissionBounds += P.Position;
        EmissionBounds += P.Position + P.Direction * 110.0;
        FlightBounds += P.Position;
        const double FlightSpan = FMath::Min(8.0, FMath::Max(0.0, double(P.Lifetime)));
        for (int32 Sample = 0; Sample <= 8; ++Sample)
            FlightBounds += EvaluatePosition(I, P.FireTime + FlightSpan * (double(Sample) / 8.0));
    }
    if (Target.bEnabled)
    {
        FlightBounds += Target.PositionAt(0.0) + FVector(Target.CapsuleRadiusCm, 0, Target.CapsuleHalfHeightCm);
        FlightBounds += Target.PositionAt(0.0) - FVector(Target.CapsuleRadiusCm, 0, Target.CapsuleHalfHeightCm);
        for (int32 S = 0; S <= 12; ++S) FlightBounds += Target.PositionAt(FlightDuration * S / 12.0);
    }
    bValid = !Points.IsEmpty();
    bTruncated |= Points.Num() >= MaxEmissions;
    Message = FString::Printf(TEXT("%d preview shots%s%s%s%s%s%s%s"), Points.Num(),
        bTruncated ? TEXT(" (preview budget reached)") : TEXT(""),
        bAim ? (Target.bEnabled ? TEXT(" | capsule target (preview)") : TEXT(" | target preview at +X 500 cm")) : TEXT(""),
        bHasUnsupportedGravity ? TEXT(" | external gravity requires PIE") : TEXT(""),
        bEstimatedEvents ? (Target.bEnabled ? TEXT(" | event timing and capsule impacts estimated") : TEXT(" | distance/expiration estimated without collisions")) : TEXT(""),
        bNeedsPIE ? TEXT(" | impact/destruction or provider events require PIE") : TEXT(""),
        bDistanceApproximationCapped ? TEXT(" | distance preview capped at 20s per projectile") : TEXT(""),
        Target.bEnabled ? TEXT(" | capsule tests: first 512 collidable shots, 8s; approximate, not World collision") : TEXT(""));
    return bValid;
}

int32 FBarragePatternPreviewModel::CountTargetHitsThrough(double Seconds) const
{
    int32 Count = 0;
    for (double T : ImpactTimes) if (T >= 0.0 && T <= Seconds) ++Count;
    return Count;
}

int32 FBarragePatternPreviewModel::CountThrough(double TimeSeconds) const
{
    if (!FMath::IsFinite(TimeSeconds)) return 0;
    int32 Low = 0, High = Points.Num();
    while (Low < High)
    {
        const int32 Mid = Low + (High - Low) / 2;
        if (Points[Mid].FireTime <= TimeSeconds + 0.000001) Low = Mid + 1;
        else High = Mid;
    }
    return Low;
}

bool FBarragePatternPreviewModel::IsAliveAt(int32 Index, double GlobalTime) const
{
    if (!Points.IsValidIndex(Index) || GlobalTime < Points[Index].FireTime || GlobalTime-Points[Index].FireTime>=Points[Index].Lifetime) return false;
    // A scheduled age child is conditional on its parent still existing. The analytic
    // preview can at least rule out known lifetime expiry (actual collision is PIE-only).
    int32 Current=Index;
    for(int32 Depth=0;Depth<8;++Depth)
    {
        const FBarragePreviewPoint& P=Points[Current];
        if(!P.bAnchored) return true;
        if(!Points.IsValidIndex(P.AnchorIndex) || P.AnchorIndex==Current) return false;
        const FBarragePreviewPoint& Parent=Points[P.AnchorIndex];
        if(P.FireTime-Parent.FireTime>=Parent.Lifetime) return false;
        Current=P.AnchorIndex;
    }
    return !Points[Current].bAnchored;
}

FVector FBarragePatternPreviewModel::EvaluatePosition(int32 Index, double GlobalTime) const
{
    return EvaluatePositionInternal(Index, GlobalTime, 0);
}

FVector FBarragePatternPreviewModel::EvaluatePositionInternal(int32 Index, double GlobalTime, int32 Depth) const
{
    if (!Points.IsValidIndex(Index)) return FVector::ZeroVector;
    const FBarragePreviewPoint& P = Points[Index];
    // Hard recursion limit protects preview from malformed parent indexes.
    FVector SpawnPosition = P.Position;
    if (P.bAnchored && Depth < 8 && Points.IsValidIndex(P.AnchorIndex) && P.AnchorIndex != Index)
        SpawnPosition = EvaluatePositionInternal(P.AnchorIndex, P.FireTime, Depth + 1) + P.RelativeAnchorOffset;
    const double Age = FMath::Max(0.0, GlobalTime - P.FireTime);
    if (P.bGroupOrbit)
    {
        const FVector Axis = P.OrbitAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        const double Rate = FMath::DegreesToRadians(double(P.GroupTurnRateDegrees));
        const double StartClock = double(P.GroupClockOffsetSeconds);
        // A released Group Orbit projectile can enter a second authored Motion
        // phase. Keep the analytic fast path for ordinary releases; reconstruct
        // only the Tracker segment at fixed substeps.
        auto EvaluateReleased = [&](FVector Position, FVector Velocity, double Seconds, double StartGlobalTime) -> FVector
        {
            if (Seconds <= 0.0) return Position;
            const FBarrageMotionProgram& Program = P.MotionProgram;
            const bool bHandoff = Program.NumPhases >= 2;
            if (!bHandoff)
            {
                FVector Acceleration = FVector::ZeroVector;
                if (P.ReleaseGravityMode == EBarrageGravityMode::Constant) Acceleration = P.ConstantAcceleration;
                else if (P.ReleaseGravityMode == EBarrageGravityMode::World) Acceleration = GetPreviewWorldGravity();
                return Position + Velocity * Seconds + Acceleration * (0.5 * Seconds * Seconds);
            }
            // An attached group releases at the common group clock; from that
            // instant the standard phase sequence operates on each projectile.
            // Staggered members use the same global age thresholds.
            const double GroupTimeOrigin = P.FireTime - StartClock;
            double Clock = StartGlobalTime - GroupTimeOrigin;
            double Cursor = 0.0;
            double TravelDistance = 0.0;
            int32 PhaseIndex = 1;
            const FBarrageMotionPhase& Initial = Program.Phases[PhaseIndex];
            Velocity = Initial.bStationary ? FVector::ZeroVector : Velocity * Initial.SpeedMultiplier;
            while (Cursor < Seconds - 1.e-9)
            {
                const FBarrageMotionPhase& Phase = Program.Phases[PhaseIndex];
                const bool bHasNext = PhaseIndex + 1 < Program.NumPhases;
                const bool bDistance = bHasNext && Phase.TransitionCondition == EBarrageMotionTransitionCondition::Distance;
                if (bHasNext && (bDistance
                    ? TravelDistance >= double(Phase.EndDistanceCm) - 1.e-7
                    : Clock >= double(Phase.EndAgeSeconds) - 1.e-8))
                {
                    const FBarrageMotionPhase& Next = Program.Phases[++PhaseIndex];
                    if (Next.bStationary) Velocity = FVector::ZeroVector;
                    else if (Phase.bStationary || Velocity.IsNearlyZero())
                        Velocity = P.Direction * P.ReleaseSpeed * Next.SpeedMultiplier;
                    else Velocity *= Next.SpeedMultiplier / Phase.SpeedMultiplier;
                    continue;
                }
                double Span = Seconds - Cursor;
                if (bHasNext && !bDistance)
                    Span = FMath::Min(Span, FMath::Max(0.0, double(Phase.EndAgeSeconds) - Clock));
                if (Phase.bTracker && P.bHasTrackerTarget) Span = FMath::Min(Span, 1.0 / 60.0);
                FVector Acceleration = FVector::ZeroVector;
                if (!Phase.bStationary && !Phase.bIgnoreGravity && Phase.Mode != EBarrageMotionMode::CurvedFlight)
                {
                    if (P.ReleaseGravityMode == EBarrageGravityMode::Constant) Acceleration = P.ConstantAcceleration;
                    else if (P.ReleaseGravityMode == EBarrageGravityMode::World) Acceleration = GetPreviewWorldGravity();
                }
                if (bDistance && !Phase.bStationary)
                {
                    const double RemainingDistance = double(Phase.EndDistanceCm) - TravelDistance;
                    auto Length = [&](double T) -> double
                    {
                        return Phase.Mode == EBarrageMotionMode::CurvedFlight ? Velocity.Size() * T
                            : FBarrageSimulation::IntegrateTravelDistance(Velocity, Acceleration, T);
                    };
                    if (Length(Span) >= RemainingDistance)
                    {
                        double Low = 0.0, High = Span;
                        for (int32 N = 0; N < 32; ++N)
                        {
                            const double Mid = (Low + High) * 0.5;
                            if (Length(Mid) >= RemainingDistance) High = Mid;
                            else Low = Mid;
                        }
                        Span = High;
                    }
                }
                if (Span <= 1.e-9) break;
                if (Phase.bTracker && P.bHasTrackerTarget)
                {
                    const double SampleTime = StartGlobalTime + Cursor;
                    FVector Aim = Target.bEnabled ? Target.PositionAt(SampleTime) : P.TrackerTargetPosition;
                    if (Target.bEnabled && Phase.bTrackerPredictLead)
                        Aim = FBarrageSimulation::PredictInterceptTarget(Position, Velocity.Size(), Aim,
                            Target.VelocityAt(SampleTime), Phase.TrackerMaxLeadSeconds);
                    Velocity = FBarrageSimulation::SteerToward(Velocity, Position, Aim, Phase.TrackerTurnRateDegrees, Span);
                }
                const FVector BeforeVelocity = Velocity;
                if (!Phase.bStationary && Phase.Mode == EBarrageMotionMode::CurvedFlight)
                {
                    FVector NewVelocity;
                    Position += FBarrageSimulation::AdvanceCurvedFlight(Velocity, Axis, Phase.TurnRateDegrees, Span, NewVelocity);
                    Velocity = NewVelocity;
                }
                else if (!Phase.bStationary)
                {
                    Position += Velocity * Span + Acceleration * (0.5 * Span * Span);
                    Velocity += Acceleration * Span;
                }
                if (Program.bTracksDistance && !Phase.bStationary)
                    TravelDistance += Phase.Mode == EBarrageMotionMode::CurvedFlight ? BeforeVelocity.Size() * Span
                        : FBarrageSimulation::IntegrateTravelDistance(BeforeVelocity, Acceleration, Span);
                Cursor += Span;
                Clock += Span;
            }
            return Position;
        };
        if (P.GroupReleaseSeconds > 0.0f && StartClock >= double(P.GroupReleaseSeconds))
        {
            const FVector CurrentCenter = P.OrbitCenter + P.GroupTravelVelocity * StartClock;
            const FVector CurrentOffset = SpawnPosition - CurrentCenter;
            const FVector Velocity = P.GroupTravelVelocity +
                FVector::CrossProduct(Axis, CurrentOffset) * Rate;
            return EvaluateReleased(SpawnPosition, Velocity, Age, P.FireTime);
        }
        const FVector InitialOffset = FQuat(Axis, -Rate * StartClock).RotateVector(
            SpawnPosition - P.OrbitCenter - P.GroupTravelVelocity * StartClock);
        const double EndClock = StartClock + Age;
        const double AttachedClock = P.GroupReleaseSeconds > 0.0f
            ? FMath::Min(EndClock, double(P.GroupReleaseSeconds)) : EndClock;
        const FVector Offset = FQuat(Axis, Rate * AttachedClock).RotateVector(InitialOffset);
        FVector Center = P.OrbitCenter + P.GroupTravelVelocity * AttachedClock;
        FVector CenterVelocity = P.GroupTravelVelocity;
        if (P.MotionProgram.NumPhases > 0 && P.MotionProgram.Phases[0].bTracker)
        {
            // One deterministic center history per Group identity, independent of
            // member spawn order, offset and preview scrub direction. Fixed steps
            // are cached once per group instead of repeated for each projectile.
            const FBarrageMotionPhase& Tracker = P.MotionProgram.Phases[0];
            constexpr double FixedStep = 1.0 / 60.0;
            constexpr int32 MaxCachedFrames = 7201; // 120 seconds of exact steering
            const double TargetTimeOrigin = P.FireTime - StartClock;
            TArray<FTrackedGroupFrame>* Frames = P.GroupIdentity != INDEX_NONE
                ? &TrackedGroupPaths.FindOrAdd(P.GroupIdentity) : nullptr;
            TArray<FTrackedGroupFrame> LocalFrames;
            if (!Frames) Frames = &LocalFrames;
            if (Frames->IsEmpty())
            {
                FTrackedGroupFrame Initial;
                Initial.Center = P.OrbitCenter;
                Initial.Velocity = P.GroupTravelVelocity;
                Frames->Add(Initial);
            }
            const int32 RequestedFullSteps = FMath::Max(0, FMath::FloorToInt(AttachedClock / FixedStep + 1.e-8));
            const int32 FullSteps = FMath::Min(RequestedFullSteps, MaxCachedFrames - 1);
            auto AdvanceCenter = [&](const FTrackedGroupFrame& From, double Time, double Span)
            {
                FTrackedGroupFrame Next = From;
                if (P.bHasTrackerTarget)
                {
                    const double SampleTime = TargetTimeOrigin + Time;
                    FVector Desired = Target.bEnabled ? Target.PositionAt(SampleTime) : P.TrackerTargetPosition;
                    if (Target.bEnabled && Tracker.bTrackerPredictLead)
                        Desired = FBarrageSimulation::PredictInterceptTarget(Next.Center, Next.Velocity.Size(),
                            Desired, Target.VelocityAt(SampleTime), Tracker.TrackerMaxLeadSeconds);
                    Next.Velocity = FBarrageSimulation::SteerToward(Next.Velocity, Next.Center, Desired,
                        Tracker.TrackerTurnRateDegrees, Span);
                }
                Next.Center += Next.Velocity * Span;
                return Next;
            };
            while (Frames->Num() <= FullSteps)
            {
                const int32 Completed = Frames->Num() - 1;
                Frames->Add(AdvanceCenter(Frames->Last(), Completed * FixedStep, FixedStep));
            }
            const FTrackedGroupFrame Base = (*Frames)[FullSteps];
            const double Remainder = FMath::Max(0.0, AttachedClock - FullSteps * FixedStep);
            const FTrackedGroupFrame Sample = Remainder > 1.e-9
                ? (FullSteps < MaxCachedFrames - 1
                    ? AdvanceCenter(Base, FullSteps * FixedStep, Remainder)
                    : FTrackedGroupFrame{Base.Center + Base.Velocity * Remainder, Base.Velocity})
                : Base;
            Center = Sample.Center;
            CenterVelocity = Sample.Velocity;
        }
        const FVector Position = Center + Offset;
        const double FreeSeconds = EndClock - AttachedClock;
        return EvaluateReleased(Position, CenterVelocity + FVector::CrossProduct(Axis, Offset) * Rate,
            FreeSeconds, GlobalTime - FreeSeconds);
    }
    auto EvaluateIndependentPhases = [&](FVector Position, FVector Velocity, double Cursor, int32 FirstPhase) -> FVector
    {
        // The compiled interpreter owns ordinary Motion and the independent
        // portion of standalone Orbit. External provider gravity and impacts
        // remain deliberately PIE-only.
        double Traveled = 0.0;
        for (int32 PhaseIndex = FirstPhase; PhaseIndex < P.MotionProgram.NumPhases && Cursor < Age - 1.e-9; ++PhaseIndex)
        {
            const FBarrageMotionPhase& Phase = P.MotionProgram.Phases[PhaseIndex];
            if (PhaseIndex > FirstPhase && !Phase.bStationary)
            {
                const FBarrageMotionPhase& Previous = P.MotionProgram.Phases[PhaseIndex - 1];
                if (Previous.bStationary || Velocity.IsNearlyZero())
                    Velocity = P.Direction * P.ReleaseSpeed * Phase.SpeedMultiplier;
                else Velocity *= Phase.SpeedMultiplier / Previous.SpeedMultiplier;
            }
            if (Phase.bStationary) Velocity = FVector::ZeroVector;
            FVector Acceleration = FVector::ZeroVector;
            if (!Phase.bIgnoreGravity && !Phase.bStationary && Phase.Mode != EBarrageMotionMode::CurvedFlight)
            {
                if (P.ReleaseGravityMode == EBarrageGravityMode::Constant) Acceleration = P.ConstantAcceleration;
                else if (P.ReleaseGravityMode == EBarrageGravityMode::World) Acceleration = GetPreviewWorldGravity();
            }
            // Tracker is a *history-dependent* steering operation. Evaluating one
            // giant age-sized steering step produces a different trajectory at
            // every requested timeline position and can appear to teleport.
            // Reconstruct the trajectory with fixed substeps, sampling the target
            // at each substep boundary. Untargeted phases keep the analytic path.
            const double PhaseEndAge = PhaseIndex + 1 < P.MotionProgram.NumPhases &&
                Phase.TransitionCondition == EBarrageMotionTransitionCondition::Age
                ? FMath::Min(Age, double(Phase.EndAgeSeconds)) : Age;
            const bool bDistanceTransition = PhaseIndex + 1 < P.MotionProgram.NumPhases &&
                Phase.TransitionCondition == EBarrageMotionTransitionCondition::Distance && !Phase.bStationary;
            while (Cursor < PhaseEndAge - 1.e-9)
            {
                double Span = PhaseEndAge - Cursor;
                if (Phase.bTracker && P.bHasTrackerTarget)
                    Span = FMath::Min(Span, 1.0 / 60.0);
                if (bDistanceTransition)
                {
                    const double Required = double(Phase.EndDistanceCm) - Traveled;
                    if (Required <= 1.e-8) break;
                    auto Length = [&](double T) -> double
                    {
                        return Phase.Mode == EBarrageMotionMode::CurvedFlight ? Velocity.Size() * T
                            : FBarrageSimulation::IntegrateTravelDistance(Velocity, Acceleration, T);
                    };
                    if (Length(Span) >= Required)
                    {
                        double Low = 0.0, High = Span;
                        for (int32 Iter = 0; Iter < 32; ++Iter)
                        {
                            const double Mid = (Low + High) * 0.5;
                            if (Length(Mid) >= Required) High = Mid;
                            else Low = Mid;
                        }
                        Span = High;
                    }
                }
                if (Span <= 1.e-9) break;
                if (Phase.bTracker && P.bHasTrackerTarget)
                {
                    const double SampleTime = P.FireTime + Cursor;
                    FVector AimPosition = Target.bEnabled ? Target.PositionAt(SampleTime) : P.TrackerTargetPosition;
                    if (Target.bEnabled && Phase.bTrackerPredictLead)
                        AimPosition = FBarrageSimulation::PredictInterceptTarget(Position, Velocity.Size(),
                            AimPosition, Target.VelocityAt(SampleTime), Phase.TrackerMaxLeadSeconds);
                    Velocity = FBarrageSimulation::SteerToward(Velocity, Position, AimPosition,
                        Phase.TrackerTurnRateDegrees, Span);
                }
                const FVector BeforeVelocity = Velocity;
                if (Phase.Mode == EBarrageMotionMode::CurvedFlight && !Phase.bStationary)
                {
                    FVector NextVelocity;
                    Position += FBarrageSimulation::AdvanceCurvedFlight(Velocity, P.OrbitAxis,
                        Phase.TurnRateDegrees, Span, NextVelocity);
                    Velocity = NextVelocity;
                }
                else if (!Phase.bStationary)
                {
                    Position += Velocity * Span + Acceleration * (0.5 * Span * Span);
                    Velocity += Acceleration * Span;
                }
                if (P.MotionProgram.bTracksDistance)
                    Traveled += Phase.bStationary ? 0.0 : Phase.Mode == EBarrageMotionMode::CurvedFlight
                        ? BeforeVelocity.Size() * Span : FBarrageSimulation::IntegrateTravelDistance(BeforeVelocity, Acceleration, Span);
                Cursor += Span;
                if (bDistanceTransition && Traveled >= double(Phase.EndDistanceCm) - 1.e-7)
                    break;
            }
            if (PhaseIndex + 1 == P.MotionProgram.NumPhases && Cursor < PhaseEndAge - 1.e-9)
                break;
        }
        return Position;
    };
    if (P.bOrbit && P.MotionProgram.NumPhases >= 2 &&
        P.MotionProgram.Phases[0].Mode == EBarrageMotionMode::PrescribedOrbit)
    {
        const FVector Axis = P.OrbitAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        const double ReleaseAge = double(P.MotionProgram.Phases[0].EndAgeSeconds);
        const double OrbitAge = FMath::Min(Age, ReleaseAge);
        const double Rate = FMath::DegreesToRadians(double(P.OrbitDegreesPerSecond));
        const FVector Center = P.bAnchored ? P.OrbitCenter + SpawnPosition - P.RelativeAnchorOffset : P.OrbitCenter;
        const FVector Rotated = FQuat(Axis, Rate * OrbitAge).RotateVector(SpawnPosition - Center);
        const FVector Position = Center + Rotated;
        if (Age <= ReleaseAge) return Position;
        const FBarrageMotionPhase& Next = P.MotionProgram.Phases[1];
        const FVector Tangent = FVector::CrossProduct(Axis, Rotated) * Rate;
        const FVector Velocity = Next.bStationary ? FVector::ZeroVector : Tangent * Next.SpeedMultiplier;
        return EvaluateIndependentPhases(Position, Velocity, ReleaseAge, 1);
    }
    if (P.MotionProgram.NumPhases > 0)
        return EvaluateIndependentPhases(SpawnPosition,
            P.Direction * P.ReleaseSpeed * P.MotionProgram.Phases[0].SpeedMultiplier, 0.0, 0);
    if (P.MovementTransitionAgeSeconds > 0.0f && Age > P.MovementTransitionAgeSeconds)
    {
        // Analytic two-stage evaluation, using the same curved-flight primitive
        // as the runtime. Collision and external gravity remain PIE-only.
        const double Pre = P.MovementTransitionAgeSeconds;
        const double Post = Age - Pre;
        const FVector ProfileVelocity = P.Direction * P.ReleaseSpeed;
        FVector PhaseEnd = SpawnPosition;
        FVector VelocityAtTransition = P.Direction * P.Speed;
        if (P.bCurved)
            PhaseEnd += FBarrageSimulation::AdvanceCurvedFlight(VelocityAtTransition, P.OrbitAxis,
                P.OrbitDegreesPerSecond, Pre, VelocityAtTransition);
        else
        {
            FVector A = FVector::ZeroVector;
            if (P.GravityMode == EBarrageGravityMode::Constant) A = P.ConstantAcceleration;
            else if (P.GravityMode == EBarrageGravityMode::World) A = GetPreviewWorldGravity();
            PhaseEnd += VelocityAtTransition * Pre + A * (0.5 * Pre * Pre);
            VelocityAtTransition += A * Pre;
        }
        if (P.MovementTransitionTo == EBarrageTransitionMovement::Stationary)
            return PhaseEnd;
        if (VelocityAtTransition.IsNearlyZero()) VelocityAtTransition = ProfileVelocity;
        if (P.MovementTransitionTo == EBarrageTransitionMovement::Curved)
        {
            FVector FinalVelocity;
            return PhaseEnd + FBarrageSimulation::AdvanceCurvedFlight(VelocityAtTransition, P.OrbitAxis,
                P.TransitionTurnRateDegrees, Post, FinalVelocity);
        }
        FVector A = FVector::ZeroVector;
        if (P.ReleaseGravityMode == EBarrageGravityMode::Constant) A = P.ConstantAcceleration;
        else if (P.ReleaseGravityMode == EBarrageGravityMode::World) A = GetPreviewWorldGravity();
        return PhaseEnd + VelocityAtTransition * Post + A * (0.5 * Post * Post);
    }
    if (P.StationaryReleaseAgeSeconds > 0.0f)
    {
        if (Age <= P.StationaryReleaseAgeSeconds) return SpawnPosition;
        const double MovingAge = Age - P.StationaryReleaseAgeSeconds;
        FVector Accel = FVector::ZeroVector;
        if (P.ReleaseGravityMode == EBarrageGravityMode::Constant) Accel = P.ConstantAcceleration;
        else if (P.ReleaseGravityMode == EBarrageGravityMode::World) Accel = GetPreviewWorldGravity();
        return SpawnPosition + P.Direction * (double(P.ReleaseSpeed) * MovingAge) + Accel * (0.5 * MovingAge * MovingAge);
    }
    if (P.bCurved)
    {
        FVector FinalVelocity;
        return SpawnPosition+FBarrageSimulation::AdvanceCurvedFlight(P.Direction*P.Speed,P.OrbitAxis,P.OrbitDegreesPerSecond,Age,FinalVelocity);
    }
    if (P.bOrbit)
    {
        const double OrbitAge = P.OrbitReleaseSeconds > 0.0f ? FMath::Min(Age, double(P.OrbitReleaseSeconds)) : Age;
        const double Rate = FMath::DegreesToRadians(double(P.OrbitDegreesPerSecond));
        const FVector Center = P.bAnchored ? P.OrbitCenter + SpawnPosition - P.RelativeAnchorOffset : P.OrbitCenter;
        const FVector Offset = SpawnPosition - Center;
        const FVector Rotated = FQuat(P.OrbitAxis.GetSafeNormal(), Rate * OrbitAge).RotateVector(Offset);
        FVector Position = Center + Rotated;
        if (P.OrbitReleaseSeconds <= 0.0f || Age <= P.OrbitReleaseSeconds) return Position;
        const double FreeAge = Age - P.OrbitReleaseSeconds;
        const FVector Tangent = FVector::CrossProduct(P.OrbitAxis.GetSafeNormal(), Rotated) * Rate;
        FVector Accel = FVector::ZeroVector;
        if (P.GravityMode == EBarrageGravityMode::Constant) Accel = P.ConstantAcceleration;
        else if (P.GravityMode == EBarrageGravityMode::World) Accel = GetPreviewWorldGravity();
        return Position + Tangent * FreeAge + Accel * (0.5 * FreeAge * FreeAge);
    }
    FVector Accel = FVector::ZeroVector;
    if (P.GravityMode == EBarrageGravityMode::Constant) Accel = P.ConstantAcceleration;
    else if (P.GravityMode == EBarrageGravityMode::World) Accel = GetPreviewWorldGravity();
    return SpawnPosition + P.Direction * (double(P.Speed) * Age) + Accel * (0.5 * Age * Age);
}
