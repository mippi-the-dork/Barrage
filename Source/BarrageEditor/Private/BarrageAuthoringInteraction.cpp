#include "BarrageAuthoringInteraction.h"
#include "BarragePatternParameterPolicy.h"
#include "BarrageFormationStack.h"
#include "BarrageEmissionStack.h"


bool FBarrageAuthoringInteraction::CanDisableActivationTrigger(const FBarragePatternLayer& Layer, FGuid Id)
{
    const FBarrageActivationBlock* Target=Layer.ActivationBlocks.FindByPredicate([Id](const FBarrageActivationBlock& B)
    {return B.BlockId==Id;});
    if(!Target)return false;
    if(Target->Kind!=EBarrageActivationBlockKind::Trigger || !Target->bEnabled)return true;
    return Layer.ActivationBlocks.ContainsByPredicate([Id](const FBarrageActivationBlock& B)
    {return B.BlockId!=Id && B.bEnabled && B.Kind==EBarrageActivationBlockKind::Trigger;});
}

bool FBarrageAuthoringInteraction::CanDisableFormationGenerator(const FBarragePatternLayer& Layer, FGuid Id)
{
    const FBarrageFormationBlock* Target=Layer.FormationBlocks.FindByPredicate([Id](const FBarrageFormationBlock& B)
    {return B.BlockId==Id;});
    if(!Target)return false;
    if(Target->Kind!=EBarrageFormationBlockKind::Generator || !Target->bEnabled)return true;
    return Layer.FormationBlocks.ContainsByPredicate([Id](const FBarrageFormationBlock& B)
    {return B.BlockId!=Id && B.bEnabled && B.Kind==EBarrageFormationBlockKind::Generator;});
}

bool FBarrageAuthoringInteraction::CanRemoveActivationBlock(const FBarragePatternLayer& Layer, FGuid Id)
{
    const FBarrageActivationBlock* Target=Layer.ActivationBlocks.FindByPredicate([Id](const FBarrageActivationBlock& B)
    {return B.BlockId==Id;});
    if(!Target)return false;
    if(Target->Kind!=EBarrageActivationBlockKind::Trigger || !Target->bEnabled)return true;
    return Layer.ActivationBlocks.ContainsByPredicate([Id](const FBarrageActivationBlock& B)
    {return B.BlockId!=Id && B.bEnabled && B.Kind==EBarrageActivationBlockKind::Trigger;});
}

bool FBarrageAuthoringInteraction::CanRemoveFormationBlock(const FBarragePatternLayer& Layer, FGuid Id)
{
    const FBarrageFormationBlock* Target=Layer.FormationBlocks.FindByPredicate([Id](const FBarrageFormationBlock& B)
    {return B.BlockId==Id;});
    if(!Target)return false;
    if(Target->Kind!=EBarrageFormationBlockKind::Generator || !Target->bEnabled)return true;
    return Layer.FormationBlocks.ContainsByPredicate([Id](const FBarrageFormationBlock& B)
    {return B.BlockId!=Id && B.bEnabled && B.Kind==EBarrageFormationBlockKind::Generator;});
}

bool FBarrageAuthoringInteraction::CanRemoveEmissionBlock(const FBarragePatternLayer& Layer, FGuid Id)
{
    const FBarrageEmissionBlock* Target=Layer.EmissionBlocks.FindByPredicate([Id](const FBarrageEmissionBlock& B)
    {return B.BlockId==Id;});
    return Target && Target->Kind!=EBarrageEmissionBlockKind::Instant;
}

bool FBarrageAuthoringInteraction::CanRemoveMotionBlock(const FBarragePatternLayer& Layer, FGuid Id)
{
    const int32 I=Layer.MotionBlocks.IndexOfByPredicate([Id](const FBarrageMotionBlock& B)
    {return B.BlockId==Id;});
    return I>=2 && Layer.MotionBlocks[I].Kind==EBarrageMotionBlockKind::Movement &&
        Layer.MotionBlocks[I-1].Kind==EBarrageMotionBlockKind::Transition;
}

bool FBarrageAuthoringInteraction::IsLayerVisible(int32 PointLayer, const TSet<int32>& HiddenLayers)
{
    return !HiddenLayers.Contains(PointLayer);
}

double FBarrageAuthoringInteraction::AdjacentShotTime(const TArray<double>& Times, double Current, bool bForward)
{
    if (!FMath::IsFinite(Current)) return 0.0;
    if (bForward)
    {
        for (double T : Times) if (T > Current + 0.0001) return T;
    }
    else
    {
        for (int32 I = Times.Num() - 1; I >= 0; --I)
            if (Times[I] < Current - 0.0001) return Times[I];
    }
    return Current;
}

bool FBarrageAuthoringInteraction::SupportsRadius(const FBarragePatternSettings& S)
{
    return FBarragePatternParameterPolicy::IsRelevant(S.Preset, TEXT("Radius"));
}

bool FBarrageAuthoringInteraction::SupportsSpan(const FBarragePatternSettings& S)
{
    return FBarragePatternParameterPolicy::IsRelevant(S.Preset, TEXT("AngleSpanDegrees"));
}

const FBarragePatternSettings* FBarrageAuthoringInteraction::SettingsFor(const UBarragePattern* Pattern, int32 Index)
{
    if (!Pattern) return nullptr;
    if (Index == INDEX_NONE) return nullptr;
    return Pattern->Layers.IsValidIndex(Index) ? &Pattern->Layers[Index].Settings : nullptr;
}

FBarragePatternSettings* FBarrageAuthoringInteraction::SettingsFor(UBarragePattern* Pattern, int32 Index)
{
    return const_cast<FBarragePatternSettings*>(SettingsFor(static_cast<const UBarragePattern*>(Pattern), Index));
}

FTransform FBarrageAuthoringInteraction::LayerTransform(const UBarragePattern* Pattern, int32 Index)
{
    if (!Pattern || !Pattern->Layers.IsValidIndex(Index)) return FTransform::Identity;
    return Pattern->Layers[Index].LocalTransform;
}

FVector FBarrageAuthoringInteraction::HandlePosition(const UBarragePattern* Pattern,
    int32 Index, EBarragePreviewHandle Handle)
{
    const FBarragePatternSettings* S = SettingsFor(Pattern, Index);
    if (!S) return FVector::ZeroVector;
    const FTransform LocalToPattern = LayerTransform(Pattern, Index);
    if (Handle == EBarragePreviewHandle::LayerPosition)
        return LocalToPattern.GetLocation();
    if (Handle == EBarragePreviewHandle::Radius)
    {
        const double A = FMath::DegreesToRadians(double(S->AngleOffsetDegrees));
        return LocalToPattern.TransformPosition(FVector(FMath::Cos(A), FMath::Sin(A), 0.0) *
            FMath::Max(0.0f, S->Radius));
    }
    if (Handle == EBarragePreviewHandle::AngleSpan)
    {
        const double A = FMath::DegreesToRadians(double(S->AngleOffsetDegrees) +
            double(S->AngleSpanDegrees) * 0.5);
        return LocalToPattern.TransformPosition(FVector(FMath::Cos(A), FMath::Sin(A), 0.0) *
            FMath::Max(100.0f, S->Radius));
    }
    return FVector::ZeroVector;
}

bool FBarrageAuthoringInteraction::Apply(UBarragePattern* Pattern, int32 Index,
    EBarragePreviewHandle Handle, const FVector& Point, bool bSnap)
{
    if (!Pattern || Point.ContainsNaN()) return false;
    FBarragePatternSettings* S = SettingsFor(Pattern, Index);
    if (!S) return false;
    if (Handle == EBarragePreviewHandle::LayerPosition)
    {
        if (!Pattern->Layers.IsValidIndex(Index)) return false;
        FVector Position = Point;
        if (bSnap)
        {
            Position.X = FMath::GridSnap(Position.X, 25.0);
            Position.Y = FMath::GridSnap(Position.Y, 25.0);
            Position.Z = FMath::GridSnap(Position.Z, 25.0);
        }
        if (Position.Equals(Pattern->Layers[Index].LocalTransform.GetLocation(), 0.001)) return false;
        Pattern->Layers[Index].LocalTransform.SetLocation(Position);
        return true;
    }
    const FVector Local = LayerTransform(Pattern, Index).InverseTransformPosition(Point);
    if (Handle == EBarragePreviewHandle::Radius && SupportsRadius(*S))
    {
        double Value = FMath::Clamp(FVector2D(Local.X, Local.Y).Size(), 0.0, 100000.0);
        if (bSnap) Value = FMath::GridSnap(Value, 25.0);
        if (FMath::IsNearlyEqual(double(S->Radius), Value, 0.001)) return false;
        S->Radius = static_cast<float>(Value);
        return true;
    }
    if (Handle == EBarragePreviewHandle::AngleSpan && SupportsSpan(*S))
    {
        const double Heading = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
        double Value = FMath::Clamp(2.0 * FMath::Abs(FMath::FindDeltaAngleDegrees(
            double(S->AngleOffsetDegrees), Heading)), 0.0, 360.0);
        if (bSnap) Value = FMath::GridSnap(Value, 15.0);
        if (FMath::IsNearlyEqual(double(S->AngleSpanDegrees), Value, 0.001)) return false;
        S->AngleSpanDegrees = static_cast<float>(Value);
        return true;
    }
    return false;
}

namespace
{
    bool BuildPresetBlocks(FBarragePatternLayer& Layer)
    {
        Layer.FormationBlocks.Reset();
        Layer.EmissionBlocks.Reset();
        if (!FBarrageFormationStackCompiler::ConvertToBlocks(Layer) ||
            !FBarrageEmissionStackCompiler::ConvertToBlocks(Layer)) return false;
        // Default Presets use the same block data as authored Layers.
        Layer.MotionBlocks[0].Movement=Layer.Settings.Movement;
        Layer.MotionBlocks[0].TurnRateDegrees=Layer.Settings.AngularVelocityDegrees;
        if (Layer.Settings.MovementTransitionAgeSeconds>0.0f)
        {
            FBarrageMotionBlock Transition;
            Transition.BlockId=FGuid::NewGuid();
            Transition.Kind=EBarrageMotionBlockKind::Transition;
            Transition.TransitionAgeSeconds=Layer.Settings.MovementTransitionAgeSeconds;
            Layer.MotionBlocks.Add(Transition);
            FBarrageMotionBlock Next;
            Next.BlockId=FGuid::NewGuid();
            Next.Movement=Layer.Settings.MovementTransitionTo==EBarrageTransitionMovement::Curved
                ? EBarrageMovementMode::Curved : Layer.Settings.MovementTransitionTo==EBarrageTransitionMovement::Stationary
                ? EBarrageMovementMode::Stationary : EBarrageMovementMode::Profile;
            Next.TurnRateDegrees=Layer.Settings.TransitionTurnRateDegrees;
            Layer.MotionBlocks.Add(Next);
        }
        else if (Layer.Settings.Movement==EBarrageMovementMode::Stationary &&
                 Layer.Settings.StationaryReleaseAgeSeconds>0.0f)
        {
            FBarrageMotionBlock Transition;
            Transition.BlockId=FGuid::NewGuid();
            Transition.Kind=EBarrageMotionBlockKind::Transition;
            Transition.TransitionAgeSeconds=Layer.Settings.StationaryReleaseAgeSeconds;
            Layer.MotionBlocks.Add(Transition);
            FBarrageMotionBlock Next;
            Next.BlockId=FGuid::NewGuid();
            Layer.MotionBlocks.Add(Next);
        }
        return true;
    }
}

int32 FBarrageAuthoringInteraction::InsertBlankLayerAfter(UBarragePattern* Pattern, int32 SelectedLayer)
{
    if (!Pattern || Pattern->Layers.Num() >= 256 ||
        (SelectedLayer != INDEX_NONE && !Pattern->Layers.IsValidIndex(SelectedLayer))) return INDEX_NONE;
    const int32 At = SelectedLayer == INDEX_NONE ? Pattern->Layers.Num() : SelectedLayer + 1;
    FBarragePatternLayer Layer = UBarragePattern::CreateDefaultLayer(TEXT("Blank Layer"));
    Layer.Settings.Formation = EBarrageFormation::Point;
    Layer.Settings.Count = 1;
    Layer.Settings.Radius = 0.0f;
    Layer.Settings.Direction = EBarrageDirectionMode::Uniform;
    Layer.Settings.DirectionAngleDegrees = 0.0f;
    Layer.FormationBlocks[0].Geometry = Layer.Settings;
    Pattern->Layers.Insert(MoveTemp(Layer), At);
    return At;
}

int32 FBarrageAuthoringInteraction::InsertLayerAbove(UBarragePattern* Pattern, int32 Selected, EBarragePatternPreset Preset)
{
    if (!Pattern || Pattern->Layers.Num()+(Preset==EBarragePatternPreset::SplitBloom?2:1)>256 ||
        (Selected!=INDEX_NONE && (Selected<0 || Selected>Pattern->Layers.Num())) ||
        static_cast<int32>(Preset)<0 || static_cast<int32>(Preset)>=25) return INDEX_NONE;
    const int32 At=Selected==INDEX_NONE ? 0 : Selected;
    FBarragePatternLayer Child=UBarragePattern::CreateDefaultLayer(FBarragePatternLibrary::GetPresetName(Preset));
    FBarragePatternSettings Recipe;
    Recipe.Preset=Preset;
    Child.Settings=FBarragePatternLibrary::MakeComposable(Recipe);
    if (Preset==EBarragePatternPreset::SplitBloom)
    {
        FBarragePatternLayer Parent=UBarragePattern::CreateDefaultLayer(TEXT("Split Bloom - Parent"));
        Parent.Settings.Formation=EBarrageFormation::Point;
        Parent.Settings.Direction=EBarrageDirectionMode::Uniform;
        Parent.Settings.Count=1;
        Parent.FormationBlocks[0].Geometry=Parent.Settings;
        Child.Name=TEXT("Split Bloom - Children");
        Child.Activation=EBarrageLayerActivation::AfterProjectileAge;
        Child.SourceLayerId=Parent.LayerId;
        Child.StartDelaySeconds=Child.Settings.ChildDelaySeconds;
        Child.ActivationBlocks[0].Trigger=EBarrageLayerActivation::AfterProjectileAge;
        Child.ActivationBlocks[0].SourceLayerId=Parent.LayerId;
        FBarrageActivationBlock Delay;
        Delay.BlockId=FGuid::NewGuid(); Delay.Kind=EBarrageActivationBlockKind::Delay;
        Delay.DelaySeconds=Child.Settings.ChildDelaySeconds;
        Child.ActivationBlocks.Add(Delay);
        Child.Settings.Emission=EBarrageEmissionMode::Instant;
        if (!BuildPresetBlocks(Child)) return INDEX_NONE;
        Pattern->Layers.Insert(MoveTemp(Parent),At);
        Pattern->Layers.Insert(MoveTemp(Child),At+1);
        return At;
    }
    if (!BuildPresetBlocks(Child)) return INDEX_NONE;
    Pattern->Layers.Insert(MoveTemp(Child),At);
    return At;
}

int32 FBarrageAuthoringInteraction::DuplicateLayer(UBarragePattern* Pattern, int32 Selected)
{
    if (!Pattern || Pattern->Layers.Num()>=256 || !Pattern->Layers.IsValidIndex(Selected)) return INDEX_NONE;
    FBarragePatternLayer Copy=Pattern->Layers[Selected];
    Copy.LayerId=FGuid::NewGuid();
    for (FBarrageFormationBlock& Block:Copy.FormationBlocks) Block.BlockId=FGuid::NewGuid();
    for (FBarrageEmissionBlock& Block:Copy.EmissionBlocks) Block.BlockId=FGuid::NewGuid();
    for (FBarrageActivationBlock& Block:Copy.ActivationBlocks) Block.BlockId=FGuid::NewGuid();
    for (FBarrageMotionBlock& Block:Copy.MotionBlocks) Block.BlockId=FGuid::NewGuid();
    Pattern->Layers.Insert(MoveTemp(Copy),Selected+1);
    return Selected+1;
}

bool FBarrageAuthoringInteraction::DeleteLayer(UBarragePattern* Pattern, int32 Index)
{
    if (!Pattern || !Pattern->Layers.IsValidIndex(Index)) return false;
    Pattern->Layers.RemoveAt(Index);
    return true;
}

int32 FBarrageAuthoringInteraction::ShiftLayer(UBarragePattern* Pattern, int32 Index, int32 Direction)
{
    if (!Pattern || !Pattern->Layers.IsValidIndex(Index) || (Direction != -1 && Direction != 1))
        return INDEX_NONE;
    const int32 Destination = Index + Direction;
    if (!Pattern->Layers.IsValidIndex(Destination)) return INDEX_NONE;
    Pattern->Layers.Swap(Index, Destination);
    return Destination;
}

FBarragePreviewCamera FBarragePreviewCamera::Make(const FBox& Bounds, int32 InPlane,
    const FVector2D& InSize, double Zoom, double Yaw, double Pitch, const FVector2D& Pan)
{
    FBarragePreviewCamera C;
    C.Plane = FMath::Clamp(InPlane, 0, 3);
    C.Size = InSize;
    C.Mid = InSize * 0.5 + Pan;
    const FVector Center = Bounds.IsValid ? Bounds.GetCenter() : FVector::ZeroVector;
    const FVector Extent = Bounds.IsValid ? Bounds.GetExtent() : FVector(100.0, 100.0, 100.0);
    auto ProjectPlane = [&](const FVector& P)->FVector2D
    {
        if (C.Plane == 1) return FVector2D(P.X, P.Z);
        if (C.Plane == 2) return FVector2D(P.Y, P.Z);
        return FVector2D(P.X, P.Y);
    };
    C.OrthoCenter = ProjectPlane(Center);
    const FVector2D E = ProjectPlane(Extent);
    C.OrthoScale = FMath::Min(InSize.X * 0.43 / FMath::Max(15.0, E.X * 1.14),
        InSize.Y * 0.43 / FMath::Max(15.0, E.Y * 1.14)) * Zoom;
    const double R = FMath::Max(30.0, Extent.Size());
    const double A = FMath::DegreesToRadians(Yaw);
    const double B = FMath::DegreesToRadians(Pitch);
    const FVector Radial(FMath::Cos(B) * FMath::Cos(A),
        FMath::Cos(B) * FMath::Sin(A), FMath::Sin(B));
    C.CameraForward = -Radial;
    C.CameraRight = FVector::CrossProduct(C.CameraForward, FVector::UpVector).GetSafeNormal();
    C.CameraUp = FVector::CrossProduct(C.CameraRight, C.CameraForward).GetSafeNormal();
    C.Focal = InSize.Y * 0.5 / FMath::Tan(FMath::DegreesToRadians(55.0) * 0.5);
    C.CameraPosition = Center + Radial * (FMath::Max(70.0, R * 2.8) / Zoom);
    return C;
}

FVector2D FBarragePreviewCamera::Project(const FVector& P) const
{
    if (Plane != 3)
    {
        FVector2D V;
        if (Plane == 1) V = FVector2D(P.X, P.Z);
        else if (Plane == 2) V = FVector2D(P.Y, P.Z);
        else V = FVector2D(P.X, P.Y);
        V -= OrthoCenter;
        return Mid + FVector2D(V.X * OrthoScale, -V.Y * OrthoScale);
    }
    const FVector V = P - CameraPosition;
    const double Depth = FMath::Max(1.0, FVector::DotProduct(V, CameraForward));
    return Mid + FVector2D(FVector::DotProduct(V, CameraRight) * Focal / Depth,
        -FVector::DotProduct(V, CameraUp) * Focal / Depth);
}

bool FBarragePreviewCamera::UnprojectOnHandlePlane(const FVector2D& Screen,
    const FVector& Anchor, FVector& Out) const
{
    if (!FMath::IsFinite(Screen.X) || !FMath::IsFinite(Screen.Y)) return false;
    const FVector2D Offset = Screen - Mid;
    if (Plane != 3)
    {
        if (OrthoScale <= UE_SMALL_NUMBER) return false;
        const double X = OrthoCenter.X + Offset.X / OrthoScale;
        const double Y = OrthoCenter.Y - Offset.Y / OrthoScale;
        if (Plane == 1) Out = FVector(X, Anchor.Y, Y);
        else if (Plane == 2) Out = FVector(Anchor.X, X, Y);
        else Out = FVector(X, Y, Anchor.Z);
        return true;
    }
    const FVector Ray = (CameraForward + CameraRight * (Offset.X / Focal) -
        CameraUp * (Offset.Y / Focal)).GetSafeNormal();
    if (FMath::Abs(Ray.Z) < 0.000001) return false;
    const double T = (Anchor.Z - CameraPosition.Z) / Ray.Z;
    if (T <= 0.0) return false;
    Out = CameraPosition + Ray * T;
    return !Out.ContainsNaN();
}
