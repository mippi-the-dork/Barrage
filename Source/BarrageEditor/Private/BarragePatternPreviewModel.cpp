#include "BarragePatternPreviewModel.h"
#include "BarrageAssets.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarrageStackPrograms.h"

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
    for (const FBarragePatternLayer& Layer : Pattern->Layers) bAim |= Layer.Settings.bAimAtTarget;
    const FVector PreviewTarget(500.0, 0.0, 0.0);
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
        P.FireTime = AbsoluteTime;
        P.OrbitCenter = Shot.OrbitCenter + Origin;
        P.bGroupOrbit = Shot.bGroupOrbit;
        P.GroupTravelVelocity = Shot.GroupTravelVelocity;
        P.GroupTurnRateDegrees = Shot.GroupTurnRateDegrees;
        P.GroupReleaseSeconds = Shot.GroupReleaseSeconds;
        P.GroupClockOffsetSeconds = Shot.GroupClockOffsetSeconds;
        P.OrbitAxis = Shot.OrbitAxis;
        P.OrbitDegreesPerSecond = Shot.OrbitDegreesPerSecond;
        P.OrbitReleaseSeconds = Shot.OrbitReleaseAgeSeconds;
        P.StationaryReleaseAgeSeconds = Shot.StationaryReleaseAgeSeconds;
        P.MotionProgram = Shot.MotionProgram;
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
    const double EventSampleStep = 1.0/60.0;
    const double MaximumEventTravelSeconds = 20.0;
    // Source shots can themselves come from an event. Breadth-first expansion
    // processes dependent chains without recursion and obeys a hard shot budget.
    for (int32 SourceIndex=0; SourceIndex<Points.Num(); ++SourceIndex)
    {
        if (Points.Num() >= MaxEmissions) { bTruncated = true; break; }
        const FBarragePreviewPoint Source = Points[SourceIndex];
        if (Source.Lifetime <= 0.0f || !FMath::IsFinite(Source.Lifetime)) continue;
        for (const FBarrageEventLayer& Rule : Playback.EventLayers)
        {
            if (Rule.InvocationId != InvocationIds[SourceIndex] || Rule.SourceLayerIndex != Source.LayerIndex) continue;
            if (Rule.Activation == EBarrageLayerActivation::OnProjectileImpact ||
                Rule.Activation == EBarrageLayerActivation::OnProjectileDeath)
            {
                bNeedsPIE = true;
                continue;
            }
            double EventTime = 0.0;
            FVector EventPosition = FVector::ZeroVector;
            bool bTriggered = false;
            if (Rule.Activation == EBarrageLayerActivation::OnProjectileExpired)
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
        Chronological.Reserve(Points.Num());
        for (int32 I=0;I<Order.Num();++I)
        {
            Remap[Order[I]]=I;
            Chronological.Add(MoveTemp(Points[Order[I]]));
        }
        for (FBarragePreviewPoint& Point : Chronological)
            if (Point.bAnchored && Remap.IsValidIndex(Point.AnchorIndex))
                Point.AnchorIndex=Remap[Point.AnchorIndex];
        Points=MoveTemp(Chronological);
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
    bValid = !Points.IsEmpty();
    bTruncated |= Points.Num() >= MaxEmissions;
    Message = FString::Printf(TEXT("%d preview shots%s%s%s%s%s%s"), Points.Num(),
        bTruncated ? TEXT(" (preview budget reached)") : TEXT(""),
        bAim ? TEXT(" | target preview at +X 500 cm") : TEXT(""),
        bHasUnsupportedGravity ? TEXT(" | external gravity requires PIE") : TEXT(""),
        bEstimatedEvents ? TEXT(" | distance/expiration estimated without collisions") : TEXT(""),
        bNeedsPIE ? TEXT(" | impact/destruction or provider events require PIE") : TEXT(""),
        bDistanceApproximationCapped ? TEXT(" | distance preview capped at 20s per projectile") : TEXT(""));
    return bValid;
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
        if (P.GroupReleaseSeconds > 0.0f && StartClock >= double(P.GroupReleaseSeconds))
        {
            const FVector CurrentCenter = P.OrbitCenter + P.GroupTravelVelocity * StartClock;
            const FVector CurrentOffset = SpawnPosition - CurrentCenter;
            const FVector Velocity = P.GroupTravelVelocity +
                FVector::CrossProduct(Axis, CurrentOffset) * Rate;
            FVector Accel = FVector::ZeroVector;
            if (P.ReleaseGravityMode == EBarrageGravityMode::Constant) Accel = P.ConstantAcceleration;
            else if (P.ReleaseGravityMode == EBarrageGravityMode::World) Accel = GetPreviewWorldGravity();
            return SpawnPosition + Velocity * Age + Accel * (0.5 * Age * Age);
        }
        const FVector InitialOffset = FQuat(Axis, -Rate * StartClock).RotateVector(
            SpawnPosition - P.OrbitCenter - P.GroupTravelVelocity * StartClock);
        const double EndClock = StartClock + Age;
        const double AttachedClock = P.GroupReleaseSeconds > 0.0f
            ? FMath::Min(EndClock, double(P.GroupReleaseSeconds)) : EndClock;
        const FVector Offset = FQuat(Axis, Rate * AttachedClock).RotateVector(InitialOffset);
        const FVector Position = P.OrbitCenter + P.GroupTravelVelocity * AttachedClock + Offset;
        const double FreeSeconds = EndClock - AttachedClock;
        FVector Accel = FVector::ZeroVector;
        if (P.ReleaseGravityMode == EBarrageGravityMode::Constant) Accel = P.ConstantAcceleration;
        else if (P.ReleaseGravityMode == EBarrageGravityMode::World) Accel = GetPreviewWorldGravity();
        return Position + (P.GroupTravelVelocity + FVector::CrossProduct(Axis, Offset) * Rate) * FreeSeconds
            + Accel * (0.5 * FreeSeconds * FreeSeconds);
    }
    if (P.MotionProgram.NumPhases>0)
    {
        // Evaluate the same authored phase intervals as the runtime. External
        // provider gravity and impacts remain deliberately PIE-only.
        FVector Position=SpawnPosition;
        FVector Velocity=P.Direction * P.ReleaseSpeed * P.MotionProgram.Phases[0].SpeedMultiplier;
        double Cursor=0.0;
        for (int32 PhaseIndex=0;PhaseIndex<P.MotionProgram.NumPhases;++PhaseIndex)
        {
            const FBarrageMotionPhase& Phase=P.MotionProgram.Phases[PhaseIndex];
            const double End=FMath::Min(Age,double(Phase.EndAgeSeconds));
            const double Span=FMath::Max(0.0,End-Cursor);
            if (PhaseIndex>0 && !Phase.bStationary)
            {
                const FBarrageMotionPhase& Previous=P.MotionProgram.Phases[PhaseIndex-1];
                if (Previous.bStationary || Velocity.IsNearlyZero())
                    Velocity=P.Direction * P.ReleaseSpeed * Phase.SpeedMultiplier;
                else Velocity *= Phase.SpeedMultiplier / Previous.SpeedMultiplier;
            }
            if (Phase.bStationary)
                Velocity=FVector::ZeroVector;
            else if (Phase.Mode==EBarrageMotionMode::CurvedFlight)
            {
                FVector NextVelocity;
                Position+=FBarrageSimulation::AdvanceCurvedFlight(Velocity,P.OrbitAxis,
                    Phase.TurnRateDegrees,Span,NextVelocity);
                Velocity=NextVelocity;
            }
            else
            {
                FVector A=FVector::ZeroVector;
                if (!Phase.bIgnoreGravity)
                {
                    if (P.ReleaseGravityMode==EBarrageGravityMode::Constant) A=P.ConstantAcceleration;
                    else if (P.ReleaseGravityMode==EBarrageGravityMode::World) A=GetPreviewWorldGravity();
                }
                Position+=Velocity*Span+A*(0.5*Span*Span);
                Velocity+=A*Span;
            }
            Cursor=End;
            if (Cursor>=Age-1.e-8) break;
        }
        return Position;
    }
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
