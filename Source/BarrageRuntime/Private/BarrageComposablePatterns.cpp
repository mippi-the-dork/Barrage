#include "BarragePatternLibrary.h"
#include "Math/RandomStream.h"
#include <cmath>

namespace
{
    using F = EBarrageFormation;
    using D = EBarrageDirectionMode;
    using E = EBarrageEmissionMode;
    using M = EBarrageFormationModifier;
    using V = EBarrageMovementMode;
    constexpr double Tau = 2.0 * PI;
    constexpr double Golden = PI * (3.0 - 2.23606797749979);
    FVector Radial(double A) { return FVector(FMath::Cos(A), FMath::Sin(A), 0); }
    double Frac(int32 I, int32 N) { return N > 1 ? double(I) / (N-1) : 0.5; }
    double Lateral(int32 I, int32 N, double W) { return (Frac(I,N)-0.5)*W; }
}

// A modifier operates on emitted formation samples, not on a named preset.
// The four original pairings retain their exact legacy sample/order semantics.
// All other formation/modifier pairs use the common sample-stage adapter below.
namespace
{
    bool UsesLegacyModifierPath(const FBarragePatternSettings& S)
    {
        return S.Modifier == M::None ||
            (S.Formation == F::Grid && (S.Modifier == M::Wave || S.Modifier == M::Alternating)) ||
            (S.Formation == F::Line && S.Modifier == M::Mirror) ||
            (S.Formation == F::Ring && S.Modifier == M::AngularGap);
    }

    bool IsInsideAngularGap(const FBarragePatternEmission& Shot, int32 Index, int32 Total,
        const FBarragePatternSettings& S)
    {
        if (S.GapFraction <= 0.0f) return false;
        double Phase;
        if (FMath::Square(Shot.LocalPosition.X) + FMath::Square(Shot.LocalPosition.Y) > 1.e-10)
        {
            const double Angle = FMath::Atan2(Shot.LocalPosition.Y, Shot.LocalPosition.X) -
                FMath::DegreesToRadians(double(S.AngleOffsetDegrees));
            Phase = FMath::Fmod(Angle / Tau, 1.0);
            if (Phase < 0.0) Phase += 1.0;
        }
        else
        {
            // Points on the axis have no azimuth. Use stable emission-order phase
            // so Point and other degenerate formations still have useful filtering.
            Phase = double(Index) / FMath::Max(1, Total);
        }
        return FMath::Min(Phase, 1.0 - Phase) < double(S.GapFraction) * 0.5;
    }

}

FBarragePatternSettings FBarragePatternLibrary::MakeComposable(const FBarragePatternSettings& Legacy)
{
    if (Legacy.bComposable) return Legacy;
    FBarragePatternSettings S = Legacy;
    S.bComposable = true;
    S.DirectionAngleDegrees=S.AngleOffsetDegrees; S.DirectionSpanDegrees=S.AngleSpanDegrees;
    S.DirectionTurns=S.Turns; S.DirectionArms=S.Arms;
    S.DirectionConeAngleDegrees=S.ConeAngleDegrees; S.bFillCone=S.bVolume;
    S.WaveTurns=S.Turns; S.OrbitReleaseSeconds=S.DelaySeconds; S.ChildDelaySeconds=S.DelaySeconds;
    S.Formation = F::Point; S.Direction = D::Uniform; S.Emission = E::Instant;
    S.Modifier = M::None; S.Movement = V::Profile;
    S.bMultipleDirections = S.bRotateWaves = S.bNaturalGroups = false;
    auto RadialMode = [&]() { S.Direction = S.bInward ? D::RadialInward : D::RadialOutward; };
    using P = EBarragePatternPreset;
    switch (S.Preset)
    {
    case P::Directed: case P::Wall: S.Formation = F::Line; break;
    case P::Burst: S.Emission = E::Sequential; break;
    case P::Fan: S.Direction = D::Spread; break;
    case P::Arc: S.Formation = F::Arc; RadialMode(); break;
    case P::RadialRing: S.Formation = F::Ring; RadialMode(); break;
    case P::Star: S.Formation = F::Spokes; RadialMode(); break;
    case P::Grid: S.Formation = F::Grid; break;
    case P::Scatter: S.Formation = F::RandomField; break;
    case P::Rain: S.Formation = F::RandomField; S.Emission = E::Sequential; S.bLegacyRandomOrder=true; break;
    case P::Spiral: S.Direction = D::Rotating; S.Emission = E::Sequential; break;
    case P::MultiArmSpiral: S.Direction = D::Rotating; S.bMultipleDirections = true; S.Emission = E::Grouped; S.bNaturalGroups = true; break;
    case P::RotatingRing: S.Formation = F::Ring; RadialMode(); S.Emission = E::Waves; S.bRotateWaves = true; break;
    case P::Sweep: S.Direction = D::Sweep; S.Emission = E::Sequential; break;
    case P::WaveCurtain: S.Formation = F::Grid; S.Depth = 1; S.Modifier = M::Wave; S.Emission = E::Grouped; S.bNaturalGroups = true; break;
    case P::Checkerboard: S.Formation = F::Grid; S.Depth = 1; S.Modifier = M::Alternating; S.Emission = E::Grouped; S.bNaturalGroups = true; break;
    case P::Rose: S.Formation = F::Flower; RadialMode(); break;
    case P::OrbitalHalo: S.Formation = F::Ring; S.Direction = D::Tangential; S.Movement = V::Orbit; S.bAimAtTarget = false; break;
    case P::SafeGap: S.Formation = F::Ring; RadialMode(); S.Modifier = M::AngularGap; break;
    case P::Pincer: S.Formation = F::Line; S.Modifier = M::Mirror; S.Direction = D::TowardCenter; break;
    case P::SplitBloom: S.Formation = F::Ring; RadialMode(); S.Emission = E::ParentThenFormation; break;
    case P::Cone: S.Direction = D::Cone; break;
    case P::Sphere: S.Formation = F::Sphere; RadialMode(); break;
    case P::Hemisphere: S.Formation = F::SphericalCap; RadialMode(); break;
    case P::Helix: S.Formation = F::Helix; RadialMode(); break;
    default: S.bComposable = false; break;
    }
    return S;
}

double FBarragePatternLibrary::EstimateComposableCount(const FBarragePatternSettings& S)
{
    if (S.Count <= 0 || S.Rows <= 0 || S.Columns <= 0 || S.Depth <= 0 || S.Arms <= 0 || S.Waves <= 0 || S.GroupSize <= 0 || S.DirectionArms<1 ||
        S.Petals < 1 || S.Petals > 32 || !FMath::IsFinite(S.GapFraction) || S.GapFraction < 0 || S.GapFraction > 1) return -1;
    if (S.Formation==F::Polygon && S.Petals<3) return -1;
    if (uint8(S.Formation)>uint8(F::Lissajous) || uint8(S.Modifier)>uint8(M::AngularGap) || uint8(S.Direction)>uint8(D::TowardCenter) || uint8(S.Emission)>uint8(E::ParentThenFormation) || uint8(S.Movement)>uint8(V::Curved)) return -1;
    if (!UsesLegacyModifierPath(S))
    {
        FBarragePatternSettings Plain = S;
        Plain.Modifier = M::None;
        const double PlainCount = EstimateComposableCount(Plain);
        if (PlainCount < 1.0 || PlainCount > 1000000.0) return -1.0;
        const int32 ParentCount = S.Emission == E::ParentThenFormation ? 1 : 0;
        const int32 FormationCount = int32(PlainCount) - ParentCount;
        switch (S.Modifier)
        {
        case M::Wave: return PlainCount;
        case M::Mirror: return ParentCount + 2.0 * FormationCount;
        case M::Alternating: return ParentCount + FMath::DivideAndRoundUp(FormationCount, 2);
        case M::AngularGap:
        {
            // Count the actual geometric wedge, not an assumed fraction of Count.
            // This also handles nonuniform formations such as Flower and Grid.
            TArray<FBarragePatternEmission> Samples;
            if (!GenerateComposable(Plain, Samples, int32(PlainCount), false, FVector::ZeroVector)) return -1.0;
            int32 Retained = ParentCount;
            for (int32 I = ParentCount; I < Samples.Num(); ++I)
                if (!IsInsideAngularGap(Samples[I], I - ParentCount, FormationCount, S)) ++Retained;
            return Retained;
        }
        default: return -1.0;
        }
    }
    double Count = S.Count;
    if (S.Formation == F::Grid)
        Count = double(S.Depth) * (S.Modifier == M::Alternating ? std::ceil(double(S.Rows)*S.Columns/2.0) : double(S.Rows)*S.Columns);
    if (S.Formation == F::Spokes || S.Formation == F::Helix) Count *= S.Arms;
    if (S.Modifier == M::Mirror) Count *= 2;
    if (S.Modifier == M::AngularGap && S.GapFraction > 0)
        Count = FMath::Max(0.0, Count - (2.0*std::ceil(Count*double(S.GapFraction)*0.5)-1.0));
    if (S.bMultipleDirections && S.Direction == D::Rotating) Count *= S.DirectionArms;
    if (S.Emission == E::Waves) Count *= S.Waves;
    if (S.Emission == E::ParentThenFormation) Count += 1;
    return Count;
}

bool FBarragePatternLibrary::GenerateComposable(const FBarragePatternSettings& S, TArray<FBarragePatternEmission>& Out,
    int32 Budget, bool bHasTarget, FVector Target)
{
    Out.Reset();
    const double Expected = EstimateComposableCount(S);
    if (!UsesLegacyModifierPath(S))
    {
        if (Expected < 1.0 || Expected > Budget || Budget <= 0 || Budget > 1000000) return false;
        FBarragePatternSettings Plain = S;
        Plain.Modifier = M::None;
        const double PlainCount = EstimateComposableCount(Plain);
        if (PlainCount < 1.0 || PlainCount > 1000000.0) return false;
        TArray<FBarragePatternEmission> BaseShots;
        if (!GenerateComposable(Plain, BaseShots, int32(PlainCount), bHasTarget, Target)) return false;
        const int32 ParentCount = S.Emission == E::ParentThenFormation ? 1 : 0;
        const int32 FormationCount = BaseShots.Num() - ParentCount;
        Out.Reserve(int32(Expected));
        if (ParentCount) Out.Add(BaseShots[0]);
        for (int32 I = ParentCount; I < BaseShots.Num(); ++I)
        {
            FBarragePatternEmission Shot = BaseShots[I];
            const int32 FormationIndex = I - ParentCount;
            switch (S.Modifier)
            {
            case M::Alternating:
                if (FormationIndex % 2 != 0) continue;
                Out.Add(Shot);
                break;
            case M::AngularGap:
                if (!IsInsideAngularGap(Shot, FormationIndex, FormationCount, S)) Out.Add(Shot);
                break;
            case M::Wave:
            {
                // Additive displacement preserves the existing formation's actual
                // Z coordinate. The legacy Grid Wave continues to use its recipe.
                const double Phase = FormationCount > 1 ? double(FormationIndex) / (FormationCount - 1) : 0.5;
                Shot.LocalPosition.Z += double(S.Amplitude) * FMath::Sin(Tau * double(S.Frequency) * Phase);
                if (S.Direction == D::RadialOutward || S.Direction == D::RadialInward || S.Direction == D::TowardCenter)
                {
                    const FVector RadialDirection = Shot.LocalPosition.GetSafeNormal(UE_SMALL_NUMBER, Shot.LocalDirection);
                    Shot.LocalDirection = (S.Direction == D::RadialOutward) ? RadialDirection : -RadialDirection;
                }
                else if (S.bAimAtTarget && bHasTarget && Shot.AnchorIndex == INDEX_NONE && !Shot.bOrbit)
                    Shot.LocalDirection = (Target - Shot.LocalPosition).GetSafeNormal(UE_SMALL_NUMBER, Shot.LocalDirection);
                Out.Add(Shot);
                break;
            }
            case M::Mirror:
            {
                for (int32 Side : { -1, 1 })
                {
                    FBarragePatternEmission Copy = Shot;
                    Copy.LocalPosition.X = Side * (Shot.LocalPosition.X + double(S.Length) * 0.5);
                    Copy.LocalDirection.X *= Side;
                    if (S.bAimAtTarget && bHasTarget && Shot.AnchorIndex == INDEX_NONE && !Shot.bOrbit)
                        Copy.LocalDirection = (Target - Copy.LocalPosition).GetSafeNormal(UE_SMALL_NUMBER, Copy.LocalDirection);
                    // With ParentThenFormation, both copies share the single parent.
                    Out.Add(Copy);
                }
                break;
            }
            default: return false;
            }
        }
        if (Out.Num() != int32(Expected)) { Out.Reset(); return false; }
        return true;
    }
    if (Expected < 1 || Expected > Budget || Budget <= 0 || Budget > 1000000 || Target.ContainsNaN()) return false;
    const float Numbers[] = { S.Radius,S.InnerRadius,S.Width,S.Height,S.Length,S.AngleOffsetDegrees,S.AngleSpanDegrees,S.Turns,
        S.AngularVelocityDegrees,S.IntervalSeconds,S.DelaySeconds,S.Amplitude,S.Frequency,S.ConeAngleDegrees,S.DirectionAngleDegrees,S.DirectionSpanDegrees,S.DirectionTurns,S.DirectionConeAngleDegrees,S.WaveTurns,S.OrbitReleaseSeconds,S.ChildDelaySeconds,S.StationaryReleaseAgeSeconds,S.MovementTransitionAgeSeconds,S.TransitionTurnRateDegrees };
    for (float X : Numbers) if (!FMath::IsFinite(X)) return false;
    if (S.Radius < 0 || S.InnerRadius < 0 || S.Width < 0 || S.Height < 0 || S.Length < 0 || S.IntervalSeconds < 0 || S.DelaySeconds < 0 || S.ConeAngleDegrees < 0 || S.ConeAngleDegrees > 179 || S.DirectionConeAngleDegrees<0 || S.DirectionConeAngleDegrees>179 || S.OrbitReleaseSeconds<0 || S.ChildDelaySeconds<0 || S.StationaryReleaseAgeSeconds<0 || S.MovementTransitionAgeSeconds<0 || (S.MovementTransitionAgeSeconds>0 && (S.Movement==V::Orbit || (S.Movement==V::Stationary && S.StationaryReleaseAgeSeconds>0)))) return false;
    // Bound work even when a gap excludes nearly every sample.
    if (S.Count > 1000000 || S.Rows > 1000000 || S.Columns > 1000000 || S.Depth > 1000000 || S.Arms > 1000000 || S.Waves > 1000000 || S.DirectionArms > 1000000) return false;
    const int32 N = S.Count;
    const double Base = FMath::DegreesToRadians(double(S.AngleOffsetDegrees));
    const double Span = FMath::DegreesToRadians(double(S.AngleSpanDegrees));
    const double Cone = FMath::DegreesToRadians(double(S.ConeAngleDegrees));
    const double Heading = FMath::DegreesToRadians(double(S.DirectionAngleDegrees));
    const double DirectionSpan = FMath::DegreesToRadians(double(S.DirectionSpanDegrees));
    const double DirectionCone = FMath::DegreesToRadians(double(S.DirectionConeAngleDegrees));
    FRandomStream Rng(S.RandomSeed);
    TArray<FBarragePatternEmission> Result;
    Result.Reserve(int32(Expected));
    auto Emit = [&](FVector P, FVector Dir, double Time, int32 Anchor, bool bOrbit, bool bStationary, bool bCurved = false)->bool
    {
        if (Result.Num() >= Budget || P.ContainsNaN() || Dir.ContainsNaN() || !FMath::IsFinite(Time) || Time < 0 || Time > 86400) return false;
        if (S.bAimAtTarget && bHasTarget && Anchor == INDEX_NONE && !bOrbit) Dir = (Target-P).GetSafeNormal(UE_SMALL_NUMBER,Dir);
        FBarragePatternEmission& Shot = Result.AddDefaulted_GetRef();
        Shot.LocalPosition=P; Shot.LocalDirection=Dir.GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
        Shot.TimeSeconds=float(Time); Shot.AnchorIndex=Anchor; Shot.bOrbit=bOrbit; Shot.bStationary=bStationary; Shot.bCurved=bCurved;
        Shot.OrbitDegreesPerSecond=(bOrbit||bCurved)?S.AngularVelocityDegrees:0.f; Shot.OrbitReleaseSeconds=bOrbit?S.OrbitReleaseSeconds:0.f;
        return true;
    };
    const bool bParent = S.Emission == E::ParentThenFormation;
    if (bParent && !Emit(FVector::ZeroVector,Radial(Heading),0,INDEX_NONE,false,false)) return false;
    const int32 WaveCount = S.Emission == E::Waves ? S.Waves : 1;
    for (int32 Wave=0; Wave<WaveCount; ++Wave)
    {
        int32 Ordinal=0;
        const double WaveAngle = S.Emission==E::Waves && S.bRotateWaves ? Tau*S.WaveTurns*double(Wave)/WaveCount : 0.0;
        const double A = Base + WaveAngle;
        const double H = Heading + WaveAngle;
        auto Sample = [&](FVector P, FVector R, int32 I, int32 Total, int32 NaturalGroup)->bool
        {
            if (S.Formation==F::Line || S.Formation==F::Grid || S.Formation==F::RandomField)
            {
                const FQuat Rotation(FVector::UpVector,WaveAngle);
                P=Rotation.RotateVector(P);
                R=P.GetSafeNormal(UE_SMALL_NUMBER,Radial(A));
            }
            const int32 Copies = S.bMultipleDirections && S.Direction==D::Rotating ? S.DirectionArms : 1;
            for (int32 Arm=0; Arm<Copies; ++Arm)
            {
                FVector Dir=Radial(H);
                switch (S.Direction)
                {
                case D::Uniform: break;
                case D::RadialOutward: Dir=R; break;
                case D::RadialInward: Dir=-R; break;
                case D::Tangential: Dir=FVector(-R.Y,R.X,0); break;
                case D::Spread: Dir=Radial(H+Lateral(I,Total,DirectionSpan)); break;
                case D::Rotating: Dir=Radial(H+Tau*(double(Arm)/Copies+S.DirectionTurns*double(I)/Total)); break;
                case D::Sweep: Dir=Radial(H-0.5*DirectionSpan*FMath::Cos(Tau*S.DirectionTurns*Frac(I,Total))); break;
                case D::Cone:
                {
                    const double Phi=Golden*I+H;
                    const double C=S.bFillCone?1.0-((I+0.5)/Total)*(1.0-FMath::Cos(DirectionCone)):FMath::Cos(DirectionCone);
                    const double T=FMath::Sqrt(FMath::Max(0.0,1-C*C)); Dir=FVector(C,T*FMath::Cos(Phi),T*FMath::Sin(Phi)); break;
                }
                case D::TowardCenter: Dir=(-P).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector); break;
                default: return false;
                }
                double Time=0;
                switch (S.Emission)
                {
                case E::Instant: break;
                case E::Sequential: Time=Ordinal*double(S.IntervalSeconds); break;
                case E::Grouped: Time=(S.bNaturalGroups?NaturalGroup:Ordinal/S.GroupSize)*double(S.IntervalSeconds); break;
                case E::Waves: Time=Wave*double(S.IntervalSeconds); break;
                case E::ParentThenFormation: Time=S.ChildDelaySeconds; break;
                default: return false;
                }
                if (!Emit(P,Dir,Time,bParent?0:INDEX_NONE,S.Movement==V::Orbit,S.Movement==V::Stationary,S.Movement==V::Curved)) return false;
                ++Ordinal;
            }
            return true;
        };
        switch (S.Formation)
        {
        case F::Point: case F::Line:
            for (int32 I=0;I<N;++I)
            {
                FVector P=S.Formation==F::Line?FVector(0,Lateral(I,N,S.Width),0):FVector::ZeroVector;
                if (S.Modifier==M::Mirror) { for (int32 Side=-1;Side<=1;Side+=2) { P.X=Side*double(S.Length)*0.5; if(!Sample(P,P.GetSafeNormal(),I,N,I)) return false; } }
                else if (!Sample(P,Radial(A),I,N,I)) return false;
            }
            break;
        case F::Arc: case F::Ring: case F::Flower:
            for (int32 I=0;I<N;++I)
            {
                const double Phase=double(I)/N;
                if(S.Modifier==M::AngularGap && FMath::Min(Phase,1-Phase)<S.GapFraction*0.5) continue;
                const double Theta=S.Formation==F::Arc?Lateral(I,N,Span):S.Formation==F::Flower?Tau*(I+0.3819660112501051)/N:Tau*Phase;
                const double Radius=S.Formation==F::Flower?S.Radius*FMath::Abs(FMath::Cos(0.5*S.Petals*Theta)):S.Radius;
                const FVector R=Radial(A+Theta); if(!Sample(R*Radius,R,I,N,I)) return false;
            }
            break;
        case F::Spokes:
            for (int32 Arm=0;Arm<S.Arms;++Arm) for(int32 I=0;I<N;++I)
            { const FVector R=Radial(A+Tau*Arm/S.Arms); if(!Sample(R*(S.InnerRadius+Frac(I,N)*S.Radius),R,I,N,I)) return false; }
            break;
        case F::Grid:
            for(int32 K=0;K<S.Depth;++K) for(int32 J=0;J<S.Rows;++J) for(int32 I=0;I<S.Columns;++I)
            {
                if(S.Modifier==M::Alternating && (I+J)%2) continue;
                const double Z=S.Modifier==M::Wave?S.Amplitude*FMath::Sin(Tau*S.Frequency*Frac(I,S.Columns)+J*0.5*PI):Lateral(J,S.Rows,S.Height);
                const FVector P(Lateral(K,S.Depth,S.Length),Lateral(I,S.Columns,S.Width),Z);
                if(!Sample(P,P.GetSafeNormal(),I,S.Columns,K*S.Rows+J)) return false;
            }
            break;
        case F::RandomField:
            for(int32 I=0;I<N;++I)
            {
                if (S.bLegacyRandomOrder)
                {
                    const FVector P(Rng.FRandRange(-0.5,0.5)*S.Length,Rng.FRandRange(-0.5,0.5)*S.Width,S.bVolume?Rng.FRandRange(-0.5,0.5)*S.Height:0.0);
                    if(!Sample(P,P.GetSafeNormal(),I,N,I)) return false;
                    continue;
                }
                const double X=Rng.FRandRange(-0.5,0.5)*S.Length;
                const double Y=Rng.FRandRange(-0.5,0.5)*S.Width;
                const double Z=S.bVolume?Rng.FRandRange(-0.5,0.5)*S.Height:0.0;
                const FVector P(X,Y,Z); if(!Sample(P,P.GetSafeNormal(),I,N,I))return false;
            }
            break;
        case F::Sphere: case F::SphericalCap:
            for(int32 I=0;I<N;++I)
            {
                const double Z=S.Formation==F::Sphere?1.0-2.0*(I+0.5)/N:1.0-((I+0.5)/N)*(1.0-FMath::Cos(Cone));
                const double R=FMath::Sqrt(FMath::Max(0.0,1-Z*Z)), Phi=Golden*I+A;
                const FVector Dir=S.Formation==F::Sphere?FVector(R*FMath::Cos(Phi),R*FMath::Sin(Phi),Z):FVector(Z,R*FMath::Cos(Phi),R*FMath::Sin(Phi));
                if(!Sample(Dir*S.Radius,Dir,I,N,I))return false;
            }
            break;
        case F::Polygon:
            for(int32 I=0;I<N;++I)
            {
                const double Along=double(I)*S.Petals/N;
                const int32 Side=int32(FMath::FloorToInt(Along));
                const double T=Along-Side;
                const FVector P0=Radial(A+Tau*double(Side)/S.Petals)*S.Radius;
                const FVector P1=Radial(A+Tau*double(Side+1)/S.Petals)*S.Radius;
                const FVector P=FMath::Lerp(P0,P1,T);
                if(!Sample(P,P.GetSafeNormal(),I,N,I))return false;
            }
            break;
        case F::Lissajous:
            for(int32 I=0;I<N;++I)
            {
                // Midpoint samples avoid giving the first projectile a special
                // zero-position fallback direction when both waves cross origin.
                const double T=Tau*(double(I)+0.5)/N;
                const FVector P(S.Radius*FMath::Sin(double(S.Petals)*T+A),
                    S.Radius*FMath::Sin(double(S.Turns)*T),
                    S.bVolume ? 0.5*double(S.Height)*FMath::Sin(double(S.Frequency)*T) : 0.0);
                // When a crossing still falls exactly at the origin, choose a
                // deterministic local curve tangent rather than global +X.
                const FVector Tangent(double(S.Petals)*S.Radius*FMath::Cos(double(S.Petals)*T+A),
                    double(S.Turns)*S.Radius*FMath::Cos(double(S.Turns)*T),
                    S.bVolume ? 0.5*double(S.Height)*double(S.Frequency)*FMath::Cos(double(S.Frequency)*T) : 0.0);
                const FVector RadialDirection=P.GetSafeNormal(UE_SMALL_NUMBER,Tangent.GetSafeNormal(UE_SMALL_NUMBER,Radial(A)));
                if(!Sample(P,RadialDirection,I,N,I))return false;
            }
            break;
        case F::Helix:
            for(int32 I=0;I<N;++I) for(int32 Arm=0;Arm<S.Arms;++Arm)
            { const FVector R=Radial(A+Tau*(S.Turns*Frac(I,N)+double(Arm)/S.Arms)); if(!Sample(FVector(R.X*S.Radius,R.Y*S.Radius,Lateral(I,N,S.Length)),R,I,N,I))return false; }
            break;
        default: return false;
        }
    }
    if(Result.Num()!=int32(Expected)) return false;
    Out=MoveTemp(Result); return true;
}
