#include "BarragePlayback.h"
#include "BarrageAimMath.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"
#include "BarrageStackPrograms.h"

bool FBarragePatternCompiler::AppendPattern(const UBarragePattern* Pattern, const FTransform& Transform,
    bool bHasTarget, const FVector& TargetWorld, double Delay, FBarrageActivePlayback& Out, int32 Budget)
{
    if (!Pattern || !FMath::IsFinite(Delay) || Delay < 0 || !Transform.IsValid()) return false;
    const int32 InvocationId = Out.NextInvocationId++;
    auto AppendLayer = [&](UBarrageProjectileProfile* Profile, const FBarragePatternSettings& Settings,
        const FTransform& LocalTransform, double Offset, int32 LayerIndex, int32 ExternalAnchor = INDEX_NONE,
        const FBarragePatternLayer* SourceLayer = nullptr) -> bool
    {
        if (!Profile) return true; // A Layer without an effective profile has no renderable shots.
        if (!LocalTransform.IsValid() || !FMath::IsFinite(Offset) || Offset < 0) return false;
        const FTransform WorldTransform = LocalTransform * Transform;
        const FVector LocalTarget = WorldTransform.InverseTransformPosition(TargetWorld);
        TArray<FBarragePatternEmission> Generated;
        // A Pattern has one authoring path. Every emission is compiled from
        // Formation Blocks followed by Emission Blocks, never a preset switch.
        if (!SourceLayer) return false;
        FBarrageMotionProgram Program;
        if (!FBarrageStackPrograms::CompileMotion(*SourceLayer, Program)) return false;
        FBarragePatternLayer WorkingLayer = *SourceLayer;
        WorkingLayer.Settings.Emission = EBarrageEmissionMode::Instant;
        // The first Movement Block controls how generated shots initially travel.
        // There is no second Pattern-generation path for Motion.
        const FBarrageMotionBlock& InitialMotion = SourceLayer->MotionBlocks[0];
        WorkingLayer.Settings.Movement = InitialMotion.Movement == EBarrageMovementMode::GroupOrbit
            ? EBarrageMovementMode::Profile : InitialMotion.Movement;
        WorkingLayer.Settings.AngularVelocityDegrees = InitialMotion.TurnRateDegrees;
        WorkingLayer.Settings.MovementTransitionAgeSeconds = 0.0f;
        WorkingLayer.Settings.StationaryReleaseAgeSeconds = 0.0f;
        if (!FBarrageFormationStackCompiler::Generate(WorkingLayer,Generated,
                Budget - Out.Shots.Num(),bHasTarget && ExternalAnchor==INDEX_NONE,LocalTarget)) return false;
        if (!FBarrageEmissionStackCompiler::Apply(*SourceLayer,Generated,
                Budget-Out.Shots.Num())) return false;
        const int32 Base = Out.Shots.Num();
        float EarliestEmission = FLT_MAX;
        for (const FBarragePatternEmission& Emission : Generated)
            EarliestEmission = FMath::Min(EarliestEmission, Emission.TimeSeconds);
        for (int32 I = 0; I < Generated.Num(); ++I)
        {
            const FBarragePatternEmission& E = Generated[I];
            FBarrageScheduledShot& Shot = Out.Shots.AddDefaulted_GetRef();
            Shot.Profile = Profile;
            Shot.AuthoredWorldDirection = WorldTransform.GetRotation().RotateVector(E.LocalDirection).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
            Shot.AimStrength = (Settings.bAimAtTarget && bHasTarget && E.AnchorIndex == INDEX_NONE && ExternalAnchor == INDEX_NONE && !E.bOrbit)
                ? Settings.AimStrength : 0.0f;
            Shot.WorldDirection = Shot.AuthoredWorldDirection;
            if (Shot.AimStrength > 0.0f)
            {
                const FVector SpawnPosition = WorldTransform.TransformPosition(E.LocalPosition);
                const FVector Toward = (TargetWorld - SpawnPosition).GetSafeNormal(UE_SMALL_NUMBER, Shot.AuthoredWorldDirection);
                Shot.WorldDirection = BarrageAimMath::BlendDirections(Shot.AuthoredWorldDirection, Toward, Shot.AimStrength);
            }
            Shot.OrbitCenter = WorldTransform.GetLocation();
            Shot.OrbitAxis = WorldTransform.GetRotation().RotateVector(FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
            Shot.WorldPosition = E.AnchorIndex == INDEX_NONE ? WorldTransform.TransformPosition(E.LocalPosition)
                : WorldTransform.TransformVector(E.LocalPosition); // offset relative to the parent's current position
            if (ExternalAnchor != INDEX_NONE)
            {
                Shot.WorldPosition -= Transform.GetLocation();
                Shot.OrbitCenter -= Transform.GetLocation();
            }
            Shot.FireTime = Delay + Offset + E.TimeSeconds;
            if (!FMath::IsFinite(Shot.FireTime) || Shot.FireTime > 86400.0) return false;
            Shot.LayerIndex = LayerIndex;
            Shot.InvocationId = InvocationId;
            Shot.OriginalOrdinal = Base + I;
            Shot.AnchorIndex = E.AnchorIndex == INDEX_NONE ? ExternalAnchor : Base + E.AnchorIndex;
            Shot.bOrbit = E.bOrbit;
            Shot.bStationary = E.bStationary;
            Shot.bCurved = E.bCurved;
            Shot.MotionProgram = Program;
            Shot.bHasTrackerTarget = bHasTarget;
            Shot.TrackerTargetPosition = TargetWorld;
            if (InitialMotion.Movement == EBarrageMovementMode::GroupOrbit)
            {
                // One kinematic frame per Layer invocation. All members share the
                // same world-space center and clock, including staggered emissions.
                Shot.bGroupOrbit = true;
                Shot.GroupIdentity = InvocationId * 256 + LayerIndex;
                Shot.GroupTravelVelocity = WorldTransform.TransformVectorNoScale(InitialMotion.GroupTravelVelocity) * InitialMotion.SpeedMultiplier;
                Shot.GroupTurnRateDegrees = InitialMotion.TurnRateDegrees;
                Shot.GroupReleaseSeconds = InitialMotion.GroupReleaseSeconds;
                // Convert authored turns into an absolute clock time at compile time.
                // No additional runtime per-projectile state or branch is required.
                if (InitialMotion.GroupReleaseRevolutions > 0.0f &&
                    !FMath::IsNearlyZero(InitialMotion.TurnRateDegrees))
                {
                    const double TurnSeconds = 360.0 * double(InitialMotion.GroupReleaseRevolutions) /
                        FMath::Abs(double(InitialMotion.TurnRateDegrees));
                    if (!FMath::IsFinite(TurnSeconds) || TurnSeconds > FLT_MAX)
                        return false;
                    if (Shot.GroupReleaseSeconds <= 0.0f || TurnSeconds < double(Shot.GroupReleaseSeconds))
                        Shot.GroupReleaseSeconds = float(TurnSeconds);
                }
                Shot.GroupClockOffsetSeconds = E.TimeSeconds - EarliestEmission;
                // Group Orbit is controlled by one shared clock. A transition
                // age on its opening phase is the age of that common formation,
                // measured from the first emission. All members detach together,
                // including staggered shots still attached at that time.
                // A previously configured time/revolution release still wins.
                if (Program.NumPhases >= 2)
                {
                    const float PhaseRelease = Program.Phases[0].EndAgeSeconds;
                    if (!FMath::IsFinite(PhaseRelease)) return false;
                    if (Shot.GroupReleaseSeconds <= 0.0f || PhaseRelease < Shot.GroupReleaseSeconds)
                        Shot.GroupReleaseSeconds = PhaseRelease;
                }
                if (Program.NumPhases == 1 && !Program.Phases[0].bTracker)
                    Shot.MotionProgram.NumPhases = 0; // ordinary Group Orbit remains on the analytic fast path
            }
            // Standalone Orbit retains its analytic center-relative path until
            // the first authored Age transition. The transition, rather than
            // the legacy preset release setting, owns its release clock.
            if (InitialMotion.Movement == EBarrageMovementMode::Orbit)
            {
                // Speed Multiplier scales orbit angular velocity for both
                // standalone orbit and orbit-to-free-flight handoffs.
                Shot.OrbitDegreesPerSecond = InitialMotion.TurnRateDegrees * InitialMotion.SpeedMultiplier;
                if (Program.NumPhases >= 2)
                    Shot.OrbitReleaseAgeSeconds = Program.Phases[0].EndAgeSeconds;
                else
                    Shot.MotionProgram.NumPhases = 0; // standalone analytic orbit
            }
            if (Program.NumPhases == 1 && FMath::IsNearlyEqual(Program.Phases[0].SpeedMultiplier, 1.0f)
                && !Program.Phases[0].bIgnoreGravity && !Program.Phases[0].bTracker)
                Shot.MotionProgram.NumPhases = 0; // fast path: compiled single phase needs no per-step program interpreter
            Shot.StationaryReleaseAgeSeconds = 0.0f;
            Shot.MovementTransitionAgeSeconds = 0.0f;
            Shot.MovementTransitionTo = EBarrageTransitionMovement::Profile;
            Shot.TransitionTurnRateDegrees = 0.0f;
            if (InitialMotion.Movement != EBarrageMovementMode::Orbit)
            {
                Shot.OrbitDegreesPerSecond = E.OrbitDegreesPerSecond;
                Shot.OrbitReleaseAgeSeconds = E.OrbitReleaseSeconds;
            }
            else if (Program.NumPhases < 2)
            {
                Shot.OrbitReleaseAgeSeconds = E.OrbitReleaseSeconds;
            }
        }
        return true;
    };
    // Dependency resolution is per Pattern invocation, including each Sequence repeat.
    // The source is resolved by GUID, never array index or name. Compile topologically,
    // keeping authored order for independent layers and bounding recursion/work.
    const int32 Num = Pattern->Layers.Num();
    if (Num > 256) return false;
    TMap<FGuid,int32> ById;
    struct FRange { int32 Begin=0, End=0; double First=0,Last=0; uint8 State=0; int32 DependencyDepth=0; bool bEventOutput=false; };
    TArray<FRange> Ranges; Ranges.SetNum(Num);
    for (int32 I=0; I<Num; ++I)
    {
        const FGuid Id=Pattern->Layers[I].LayerId;
        if (!Id.IsValid() || ById.Contains(Id)) return false;
        ById.Add(Id,I);
    }
    TFunction<bool(int32,int32)> CompileLayer;
    CompileLayer = [&](int32 I,int32 Depth)->bool
    {
        if (Depth > 8) return false;
        FRange& Range=Ranges[I];
        if (Range.State==2) return true;
        if (Range.State==1) return false; // cycle
        Range.State=1;
        const FBarragePatternLayer& L=Pattern->Layers[I];
        if (!L.bEnabled) { Range.State=2; return true; }
        if (!FBarrageStackPrograms::ValidateActivation(L) || !L.LocalTransform.IsValid()) return false;
        FBarrageMotionProgram CheckedMotion;
        if (!FBarrageStackPrograms::CompileMotion(L,CheckedMotion)) return false;

        // Resolve every referenced source before compiling this Layer. Block order
        // never changes the Layer DAG, and dependencies are stable GUIDs.
        TArray<int32> Sources;
        Sources.Init(INDEX_NONE,L.ActivationBlocks.Num());
        for (int32 TriggerIndex=0;TriggerIndex<L.ActivationBlocks.Num();++TriggerIndex)
        {
            const FBarrageActivationBlock& B=L.ActivationBlocks[TriggerIndex];
            if (!B.bEnabled || B.Kind!=EBarrageActivationBlockKind::Trigger ||
                B.Trigger==EBarrageLayerActivation::PatternStart) continue;
            const int32* Found=ById.Find(B.SourceLayerId);
            if (!Found || !CompileLayer(*Found,Depth+1)) return false;
            Sources[TriggerIndex]=*Found;
            Range.DependencyDepth=FMath::Max(Range.DependencyDepth,Ranges[*Found].DependencyDepth+1);
            if (Range.DependencyDepth>8) return false;
            if (Ranges[*Found].bEventOutput &&
                (B.Trigger==EBarrageLayerActivation::AfterLayerStarts ||
                 B.Trigger==EBarrageLayerActivation::AfterLayerFinishesEmitting ||
                 B.Trigger==EBarrageLayerActivation::AfterProjectileAge)) return false;
        }
        float DelaySeconds=0.0f;
        for (const FBarrageActivationBlock& B:L.ActivationBlocks)
            if (B.bEnabled && B.Kind==EBarrageActivationBlockKind::Delay)
                DelaySeconds+=B.DelaySeconds;
        int32 EveryNth=1;
        for (const FBarrageActivationBlock& B:L.ActivationBlocks)
            if (B.bEnabled && B.Kind==EBarrageActivationBlockKind::Filter)
            {
                int32 A = EveryNth, C = B.EveryNth;
                while (C != 0) { const int32 R = A % C; A = C; C = R; }
                EveryNth = int32(int64(EveryNth) / A * B.EveryNth);
            }
        int32 ScheduledActivationOrdinal=0;
        auto KeepScheduled=[&]() -> bool
        {
            return (ScheduledActivationOrdinal++ % EveryNth)==0;
        };
        Range.Begin=Out.Shots.Num();
        UBarrageProjectileProfile* Profile=L.Projectile?L.Projectile.Get():Pattern->Projectile.Get();
        auto Append=[&](double Offset,int32 Parent)->bool
        {
            return AppendLayer(Profile,L.Settings,L.LocalTransform,Offset,I,Parent,&L);
        };
        // ALL combines known scheduled prerequisites into a single firing instant.
        // Independent event sources have no shared projectile context, so ALL of
        // those is deliberately rejected by validation rather than guessed.
        double AllTime=0.0;
        bool bAllHaveSources=true;
        for (int32 BlockIndex=0;BlockIndex<L.ActivationBlocks.Num();++BlockIndex)
        {
            const FBarrageActivationBlock& B=L.ActivationBlocks[BlockIndex];
            if (!B.bEnabled || B.Kind!=EBarrageActivationBlockKind::Trigger) continue;
            const int32 Source=Sources[BlockIndex];
            const FRange* Parent=Source==INDEX_NONE?nullptr:&Ranges[Source];
            if (Parent && Parent->Begin==Parent->End && !Parent->bEventOutput)
            {
                bAllHaveSources=false;
                continue;
            }
            double EventTime=0.0;
            switch (B.Trigger)
            {
            case EBarrageLayerActivation::PatternStart:
                EventTime=0.0;
                break;
            case EBarrageLayerActivation::AfterLayerStarts:
            case EBarrageLayerActivation::AfterLayerFinishesEmitting:
                if (!Parent || Parent->Begin==Parent->End) { bAllHaveSources=false; continue; }
                EventTime=(B.Trigger==EBarrageLayerActivation::AfterLayerStarts?Parent->First:Parent->Last)-Delay;
                break;
            case EBarrageLayerActivation::AfterProjectileAge:
                if (!Parent) return false;
                for (int32 ParentShot=Parent->Begin;ParentShot<Parent->End;++ParentShot)
                    if (KeepScheduled() && !Append(Out.Shots[ParentShot].FireTime-Delay+DelaySeconds,ParentShot)) return false;
                continue;
            case EBarrageLayerActivation::AfterProjectileDistance:
            case EBarrageLayerActivation::OnProjectileImpact:
            case EBarrageLayerActivation::OnProjectileDeath:
            case EBarrageLayerActivation::OnProjectileExpired:
            {
                if (!Parent || !Profile) return false;
                if (Parent->Begin==Parent->End && !Parent->bEventOutput) continue;
                if (Out.EventLayers.Num()>=64) return false;
                FBarrageEventLayer Event;
                Event.Activation=B.Trigger;
                Event.InvocationId=InvocationId;
                Event.SourceLayerIndex=Source;
                Event.LayerIndex=I;
                Event.DistanceCm=B.DistanceCm;
                Event.StartDelaySeconds=DelaySeconds;
                Event.EveryNth=EveryNth;
                const int32 TemplateStart=Out.Shots.Num();
                if (!Append(0.0,INDEX_NONE)) return false;
                for(int32 J=TemplateStart;J<Out.Shots.Num();++J)
                {
                    FBarrageScheduledShot Shot=Out.Shots[J];
                    if (Shot.AnchorIndex!=INDEX_NONE) return false;
                    Shot.WorldPosition-=Transform.GetLocation();
                    Shot.OrbitCenter-=Transform.GetLocation();
                    Shot.FireTime-=Delay;
                    Event.TemplateShots.Add(MoveTemp(Shot));
                }
                Out.Shots.SetNum(TemplateStart,EAllowShrinking::No);
                int32 Total=Out.Shots.Num();
                for(const FBarrageEventLayer& Existing:Out.EventLayers) Total+=Existing.TemplateShots.Num();
                if(Event.TemplateShots.IsEmpty() || Event.TemplateShots.Num()>Budget-Total) return false;
                Out.EventLayers.Add(MoveTemp(Event));
                Range.bEventOutput=true;
                continue;
            }
            default: return false;
            }
            if (L.ActivationCombination==EBarrageActivationCombination::All)
                AllTime=FMath::Max(AllTime,EventTime);
            else if (KeepScheduled() && !Append(EventTime+DelaySeconds,INDEX_NONE)) return false;
        }
        if (L.ActivationCombination==EBarrageActivationCombination::All && bAllHaveSources &&
            KeepScheduled() && !Append(AllTime+DelaySeconds,INDEX_NONE)) return false;
        Range.End=Out.Shots.Num();
        if(Range.Begin<Range.End)
        {
            Range.First=Range.Last=Out.Shots[Range.Begin].FireTime;
            for(int32 J=Range.Begin+1;J<Range.End;++J)
            {
                Range.First=FMath::Min(Range.First,Out.Shots[J].FireTime);
                Range.Last=FMath::Max(Range.Last,Out.Shots[J].FireTime);
            }
        }
        Range.State=2;
        return true;
    };
    for (int32 I=0; I<Num; ++I) if (!CompileLayer(I,0)) return false;
    return true;
}

void FBarragePatternCompiler::Finalize(FBarrageActivePlayback& Out)
{
    Out.Shots.Sort([](const FBarrageScheduledShot& A, const FBarrageScheduledShot& B)
    {
        return A.FireTime == B.FireTime ? A.OriginalOrdinal < B.OriginalOrdinal : A.FireTime < B.FireTime;
    });
    TArray<int32> Remap;
    Remap.SetNumUninitialized(Out.Shots.Num());
    for (int32 I = 0; I < Out.Shots.Num(); ++I) Remap[Out.Shots[I].OriginalOrdinal] = I;
    for (FBarrageScheduledShot& Shot : Out.Shots)
    {
        if (Shot.AnchorIndex != INDEX_NONE) Shot.AnchorIndex = Remap.IsValidIndex(Shot.AnchorIndex) ? Remap[Shot.AnchorIndex] : INDEX_NONE;
    }
    Out.SpawnedHandles.SetNum(Out.Shots.Num());
    Out.ElapsedSeconds = 0.0;
    Out.NextShot = 0;
}

bool FBarragePatternCompiler::CompilePattern(const UBarragePattern* Pattern, const FTransform& Transform,
    bool bHasTarget, const FVector& TargetWorld, FBarrageActivePlayback& Out, int32 Budget)
{
    Out = FBarrageActivePlayback();
    if (Budget <= 0 || Budget > 1000000 || !AppendPattern(Pattern, Transform, bHasTarget, TargetWorld, 0, Out, Budget) || Out.Shots.IsEmpty())
    {
        Out = FBarrageActivePlayback();
        return false;
    }
    Finalize(Out);
    return true;
}

bool FBarragePatternCompiler::CompileSequence(const UBarrageSequence* Sequence, const FTransform& Transform,
    bool bHasTarget, const FVector& TargetWorld, FBarrageActivePlayback& Out, int32 Budget)
{
    Out = FBarrageActivePlayback();
    if (!Sequence || !Transform.IsValid() || Budget <= 0 || Budget > 1000000) return false;
    for (const FBarrageSequenceEntry& Entry : Sequence->Entries)
    {
        if (!Entry.Pattern || Entry.RepeatCount <= 0 || Entry.RepeatCount > 256 ||
            !FMath::IsFinite(Entry.StartTimeSeconds) || Entry.StartTimeSeconds < 0 ||
            !FMath::IsFinite(Entry.RepeatIntervalSeconds) || Entry.RepeatIntervalSeconds <= 0 ||
            !Entry.LocalTransform.IsValid())
        {
            Out = FBarrageActivePlayback();
            return false;
        }
        for (int32 Repeat = 0; Repeat < Entry.RepeatCount; ++Repeat)
        {
            if (!AppendPattern(Entry.Pattern, Entry.LocalTransform * Transform, bHasTarget, TargetWorld,
                double(Entry.StartTimeSeconds) + Repeat * double(Entry.RepeatIntervalSeconds), Out, Budget))
            {
                Out = FBarrageActivePlayback();
                return false;
            }
        }
    }
    if (Out.Shots.IsEmpty()) return false;
    Finalize(Out);
    return true;
}
