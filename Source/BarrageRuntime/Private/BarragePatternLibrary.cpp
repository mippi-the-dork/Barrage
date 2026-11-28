#include "BarragePatternLibrary.h"
#include "BarrageAimMath.h"
#include "Math/RandomStream.h"
#include <cmath>

namespace
{
    constexpr double Tau = 2.0 * PI;
    constexpr double TrueGoldenAngle = PI * (3.0 - 2.23606797749979);

    FVector Radial(double Angle)
    {
        return FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0);
    }
    double Fraction(int32 Index, int32 Count)
    {
        return Count > 1 ? double(Index) / double(Count - 1) : 0.5;
    }
    bool Finite(float Number) { return FMath::IsFinite(Number); }
}

const TCHAR* FBarragePatternLibrary::GetPresetName(EBarragePatternPreset Preset)
{
    static const TCHAR* Names[] = {
        TEXT("Directed"), TEXT("Burst"), TEXT("Fan"), TEXT("Arc"), TEXT("Radial Ring"),
        TEXT("Star"), TEXT("Wall"), TEXT("Grid"), TEXT("Scatter"), TEXT("Rain"),
        TEXT("Spiral"), TEXT("Multi-Arm Spiral"), TEXT("Rotating Ring"), TEXT("Sweep"),
        TEXT("Wave Curtain"), TEXT("Checkerboard"), TEXT("Rose"), TEXT("Orbital Halo"),
        TEXT("Safe Gap"), TEXT("Pincer"), TEXT("Split Bloom"), TEXT("Cone"),
        TEXT("Sphere"), TEXT("Hemisphere"), TEXT("Helix")
    };
    const int32 Index = static_cast<int32>(Preset);
    return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("Unknown");
}

double FBarragePatternLibrary::EstimateEmissionCount(const FBarragePatternSettings& S)
{
    if (S.bComposable) return EstimateComposableCount(S);
    if (S.Count <= 0 || S.Rows <= 0 || S.Columns <= 0 || S.Depth <= 0 || S.Arms <= 0 || S.Waves <= 0 ||
        S.Petals < 1 || S.Petals > 32 || !FMath::IsFinite(S.GapFraction) ||
        S.GapFraction < 0.0f || S.GapFraction > 1.0f)
        return -1.0;
    const double Count = double(S.Count);
    switch (S.Preset)
    {
    case EBarragePatternPreset::Star:
    case EBarragePatternPreset::MultiArmSpiral:
    case EBarragePatternPreset::Helix: return Count * double(S.Arms);
    case EBarragePatternPreset::Grid: return double(S.Rows) * double(S.Columns) * double(S.Depth);
    case EBarragePatternPreset::WaveCurtain: return double(S.Rows) * double(S.Columns);
    case EBarragePatternPreset::Checkerboard:
        return std::ceil(double(S.Rows) * double(S.Columns) * 0.5);
    case EBarragePatternPreset::RotatingRing: return Count * double(S.Waves);
    case EBarragePatternPreset::Pincer: return Count * 2.0;
    case EBarragePatternPreset::SafeGap:
        // A strict exclusion around 0 degrees. Shots at the exact edges survive.
        // Count is an angular sample budget here, not the final number of shots.
        if (S.GapFraction <= 0.0f) return Count;
        return FMath::Max(0.0, Count - (2.0 * std::ceil(
            Count * (double(S.GapFraction) * 0.5)) - 1.0));
    case EBarragePatternPreset::SplitBloom: return Count + 1.0;
    default: return Count;
    }
}

bool FBarragePatternLibrary::Generate(const FBarragePatternSettings& S, TArray<FBarragePatternEmission>& Out,
    int32 MaxEmissions, bool bHasTarget, FVector LocalTarget)
{
    if (S.bComposable) return GenerateComposable(S, Out, MaxEmissions, bHasTarget, LocalTarget);
    Out.Reset();
    if (S.Count <= 0 || S.Rows <= 0 || S.Columns <= 0 || S.Depth <= 0 || S.Arms <= 0 || S.Waves <= 0 ||
        MaxEmissions <= 0 || MaxEmissions > 1000000 ||
        !Finite(S.Radius) || !Finite(S.InnerRadius) || !Finite(S.Width) || !Finite(S.Height) || !Finite(S.Length) ||
        !Finite(S.AngleOffsetDegrees) || !Finite(S.AngleSpanDegrees) || !Finite(S.Turns) ||
        !Finite(S.AngularVelocityDegrees) || !Finite(S.IntervalSeconds) || !Finite(S.DelaySeconds) ||
        !Finite(S.Amplitude) || !Finite(S.Frequency) || !Finite(S.GapFraction) || !Finite(S.ConeAngleDegrees) ||
        S.Radius < 0 || S.InnerRadius < 0 || S.Width < 0 || S.Height < 0 || S.Length < 0 ||
        S.IntervalSeconds < 0 || S.DelaySeconds < 0 || S.GapFraction < 0 || S.GapFraction > 1 ||
        S.ConeAngleDegrees < 0 || S.ConeAngleDegrees > 179 ||
        S.Petals < 1 || S.Petals > 32 || LocalTarget.ContainsNaN() ||
        !FMath::IsFinite(S.AimStrength) || S.AimStrength < 0.0f || S.AimStrength > 1.0f) return false;

    // Never silently reduce the author's requested spawn count. Preflight the emission
    // budget before entering any loop; all products are evaluated as doubles to avoid
    // integer overflow on malicious or accidental extreme settings.
    const double Expected = EstimateEmissionCount(S);
    if (Expected > MaxEmissions) return false;
    const int32 N = S.Count;
    const int32 Rows = S.Rows;
    const int32 Cols = S.Columns;
    const int32 Depth = S.Depth;
    const int32 Arms = S.Arms;
    const int32 Waves = S.Waves;
    const double Base = FMath::DegreesToRadians(double(S.AngleOffsetDegrees));
    const double Span = FMath::DegreesToRadians(double(S.AngleSpanDegrees));
    const double Cone = FMath::DegreesToRadians(double(S.ConeAngleDegrees));
    FRandomStream Rng(S.RandomSeed);
    Out.Reserve(FMath::Min(MaxEmissions, 4096));

    auto Emit = [&](FVector Position, FVector Direction, double Time, int32 Anchor = INDEX_NONE, bool bOrbit = false) -> bool
    {
        if (Out.Num() >= MaxEmissions || Position.ContainsNaN() || Direction.ContainsNaN() ||
            !FMath::IsFinite(Time) || Time < 0 || Time > 86400.0) return false;
        if (S.bAimAtTarget && bHasTarget && Anchor == INDEX_NONE && !bOrbit)
        {
            const FVector Original = Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
            const FVector Toward = (LocalTarget - Position).GetSafeNormal(UE_SMALL_NUMBER, Original);
            Direction = BarrageAimMath::BlendDirections(Original, Toward, S.AimStrength);
        }
        FBarragePatternEmission E;
        E.LocalPosition = Position;
        E.LocalDirection = Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        E.TimeSeconds = static_cast<float>(Time);
        E.AnchorIndex = Anchor;
        E.bOrbit = bOrbit;
        E.OrbitDegreesPerSecond = bOrbit ? S.AngularVelocityDegrees : 0.0f;
        E.OrbitReleaseSeconds = bOrbit ? S.DelaySeconds : 0.0f;
        Out.Add(E);
        return true;
    };
    auto EmitRing = [&](int32 Count, double Offset, double Radius, double Time)->bool
    {
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector R = Radial(Offset + Tau * I / Count);
            if (!Emit(R * Radius, S.bInward ? -R : R, Time)) return false;
        }
        return true;
    };
    auto Lateral = [&](int32 Index, int32 Total, double Range)->double
    {
        return (Fraction(Index, Total) - 0.5) * Range;
    };

    switch (S.Preset)
    {
    case EBarragePatternPreset::Directed:
        for (int32 I = 0; I < N; ++I)
            if (!Emit(FVector(0, Lateral(I, N, S.Width), 0), Radial(Base), 0)) return false;
        break;
    case EBarragePatternPreset::Burst:
        for (int32 I = 0; I < N; ++I) if (!Emit(FVector::ZeroVector, Radial(Base), I * double(S.IntervalSeconds))) return false;
        break;
    case EBarragePatternPreset::Fan:
        for (int32 I = 0; I < N; ++I)
            if (!Emit(FVector::ZeroVector, Radial(Base + Lateral(I, N, Span)), 0)) return false;
        break;
    case EBarragePatternPreset::Arc:
        for (int32 I = 0; I < N; ++I)
        {
            const FVector R = Radial(Base + Lateral(I, N, Span));
            if (!Emit(R * S.Radius, S.bInward ? -R : R, 0)) return false;
        }
        break;
    case EBarragePatternPreset::RadialRing:
        if (!EmitRing(N, Base, S.Radius, 0)) return false;
        break;
    case EBarragePatternPreset::Star:
        for (int32 Arm = 0; Arm < Arms; ++Arm)
            for (int32 I = 0; I < N; ++I)
            {
                const FVector R = Radial(Base + Tau * Arm / Arms);
                const double Distance = S.InnerRadius + Fraction(I, N) * S.Radius;
                if (!Emit(R * Distance, S.bInward ? -R : R, 0)) return false;
            }
        break;
    case EBarragePatternPreset::Wall:
        for (int32 I = 0; I < N; ++I)
            if (!Emit(FVector(0, Lateral(I, N, S.Width), 0), Radial(Base), 0)) return false;
        break;
    case EBarragePatternPreset::Grid:
        for (int32 K = 0; K < Depth; ++K)
            for (int32 J = 0; J < Rows; ++J)
                for (int32 I = 0; I < Cols; ++I)
                    if (!Emit(FVector(Lateral(K, Depth, S.Length), Lateral(I, Cols, S.Width),
                        Lateral(J, Rows, S.Height)), Radial(Base), 0)) return false;
        break;
    case EBarragePatternPreset::Scatter:
        for (int32 I = 0; I < N; ++I)
        {
            const double X = Rng.FRandRange(-0.5, 0.5) * S.Length;
            const double Y = Rng.FRandRange(-0.5, 0.5) * S.Width;
            const double Z = S.bVolume ? Rng.FRandRange(-0.5, 0.5) * S.Height : 0.0;
            if (!Emit(FVector(X, Y, Z), Radial(Base), 0)) return false;
        }
        break;
    case EBarragePatternPreset::Rain:
        for (int32 I = 0; I < N; ++I)
            if (!Emit(FVector(Rng.FRandRange(-0.5,0.5)*S.Length, Rng.FRandRange(-0.5,0.5)*S.Width,
                S.bVolume ? Rng.FRandRange(-0.5,0.5)*S.Height : 0.0), Radial(Base), I * double(S.IntervalSeconds))) return false;
        break;
    case EBarragePatternPreset::Spiral:
        for (int32 I = 0; I < N; ++I)
            if (!Emit(FVector::ZeroVector, Radial(Base + Tau * S.Turns * double(I) / FMath::Max(1, N)), I * double(S.IntervalSeconds))) return false;
        break;
    case EBarragePatternPreset::MultiArmSpiral:
        for (int32 I = 0; I < N; ++I)
            for (int32 Arm = 0; Arm < Arms; ++Arm)
                if (!Emit(FVector::ZeroVector, Radial(Base + Tau * (double(Arm)/Arms + S.Turns * double(I) / FMath::Max(1,N))),
                    I * double(S.IntervalSeconds))) return false;
        break;
    case EBarragePatternPreset::RotatingRing:
        for (int32 Wave = 0; Wave < Waves; ++Wave)
            if (!EmitRing(N, Base + Tau * S.Turns * Wave / Waves, S.Radius, Wave * double(S.IntervalSeconds))) return false;
        break;
    case EBarragePatternPreset::Sweep:
        for (int32 I = 0; I < N; ++I)
        {
            // Cosine oscillation gives zero velocity at the sweep endpoints.
            const double Swing = -FMath::Cos(Tau * S.Turns * Fraction(I,N));
            if (!Emit(FVector::ZeroVector, Radial(Base + 0.5 * Span * Swing), I * double(S.IntervalSeconds))) return false;
        }
        break;
    case EBarragePatternPreset::WaveCurtain:
        for (int32 J = 0; J < Rows; ++J)
            for (int32 I = 0; I < Cols; ++I)
            {
                const double Y = Lateral(I, Cols, S.Width);
                const double Z = S.Amplitude * FMath::Sin(Tau * S.Frequency * Fraction(I,Cols) + J * 0.5 * PI);
                if (!Emit(FVector(0, Y, Z), Radial(Base), J * double(S.IntervalSeconds))) return false;
            }
        break;
    case EBarragePatternPreset::Checkerboard:
        for (int32 J = 0; J < Rows; ++J)
            for (int32 I = 0; I < Cols; ++I)
                if ((I+J)%2 == 0 && !Emit(FVector(0, Lateral(I, Cols, S.Width),
                    Lateral(J, Rows, S.Height)), Radial(Base), J * double(S.IntervalSeconds))) return false;
        break;
    case EBarragePatternPreset::Rose:
        // A signed r=cos(Petals*theta) rose traces two petals per even parameter
        // and retraces an odd-petal rose twice. Use a non-negative radial envelope
        // instead: |cos(Petals*theta/2)| has exactly Petals lobes in [0, 2pi).
        // Count now means total shots, never shots per lobe or duplicated samples.
        for (int32 I = 0; I < N; ++I)
        {
            // A fractional sample offset avoids multiple shots at exactly the
            // center of the flower when Count is a multiple of Petals.
            const double Theta = Tau * (double(I) + 0.3819660112501051) / double(N);
            const double Radius = double(S.Radius) * FMath::Abs(FMath::Cos(0.5 * S.Petals * Theta));
            const FVector Direction = Radial(Base + Theta);
            if (!Emit(Direction * Radius, S.bInward ? -Direction : Direction, 0)) return false;
        }
        break;
    case EBarragePatternPreset::OrbitalHalo:
        for (int32 I = 0; I < N; ++I)
        {
            const FVector R = Radial(Base + Tau * I / N);
            if (!Emit(R * S.Radius, FVector(-R.Y, R.X, 0), 0, INDEX_NONE, true)) return false;
        }
        break;
    case EBarragePatternPreset::SafeGap:
        for (int32 I = 0; I < N; ++I)
        {
            const double Phase = double(I) / N;
            // Gap centered at angular offset (0), wrapped around the 0/1 seam.
            if (FMath::Min(Phase, 1.0 - Phase) < S.GapFraction * 0.5) continue;
            const FVector R = Radial(Base + Tau * Phase);
            if (!Emit(R * S.Radius, S.bInward ? -R : R, 0)) return false;
        }
        break;
    case EBarragePatternPreset::Pincer:
        for (int32 I = 0; I < N; ++I)
        {
            const double Y = Lateral(I, N, S.Width);
            for (int32 Side = -1; Side <= 1; Side += 2)
            {
                const FVector Pos(Side * double(S.Length) * 0.5, Y, 0);
                const FVector Dir = (FVector::ZeroVector - Pos).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
                if (!Emit(Pos, Dir, 0)) return false;
            }
        }
        break;
    case EBarragePatternPreset::SplitBloom:
        {
            const int32 Parent = Out.Num();
            if (!Emit(FVector::ZeroVector, Radial(Base), 0)) return false;
            for (int32 I = 0; I < N; ++I)
            {
                const FVector R = Radial(Base + Tau * I / N);
                if (!Emit(R * S.Radius, S.bInward ? -R : R, S.DelaySeconds, Parent)) return false;
            }
        }
        break;
    case EBarragePatternPreset::Cone:
        for (int32 I = 0; I < N; ++I)
        {
            const double Phi = TrueGoldenAngle * I + Base;
            const double U = (I + 0.5) / N;
            const double CosTheta = S.bVolume ? 1.0 - U * (1.0 - FMath::Cos(Cone)) : FMath::Cos(Cone);
            const double SinTheta = FMath::Sqrt(FMath::Max(0.0, 1.0 - CosTheta * CosTheta));
            const FVector D(CosTheta, SinTheta * FMath::Cos(Phi), SinTheta * FMath::Sin(Phi));
            if (!Emit(FVector::ZeroVector, D, 0)) return false;
        }
        break;
    case EBarragePatternPreset::Sphere:
        for (int32 I = 0; I < N; ++I)
        {
            const double Z = 1.0 - 2.0 * (I + 0.5) / N;
            const double R = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
            const double A = I * TrueGoldenAngle + Base;
            const FVector D(R*FMath::Cos(A), R*FMath::Sin(A), Z);
            if (!Emit(D * S.Radius, S.bInward ? -D : D, 0)) return false;
        }
        break;
    case EBarragePatternPreset::Hemisphere:
        for (int32 I = 0; I < N; ++I)
        {
            const double CosTheta = 1.0 - ((I + 0.5) / N) * (1.0 - FMath::Cos(Cone));
            const double R = FMath::Sqrt(FMath::Max(0.0, 1.0 - CosTheta * CosTheta));
            const double A = I * TrueGoldenAngle + Base;
            const FVector D(CosTheta, R*FMath::Cos(A), R*FMath::Sin(A));
            if (!Emit(D * S.Radius, S.bInward ? -D : D, 0)) return false;
        }
        break;
    case EBarragePatternPreset::Helix:
        for (int32 I = 0; I < N; ++I)
        {
            const double T = Fraction(I,N);
            for (int32 Arm = 0; Arm < Arms; ++Arm)
            {
                const double A = Base + Tau * (S.Turns * T + double(Arm) / Arms);
                const FVector R = Radial(A);
                if (!Emit(FVector(R.X*S.Radius,R.Y*S.Radius,Lateral(I,N,S.Length)), S.bInward ? -R : R, 0)) return false;
            }
        }
        break;
    default: return false;
    }
    return Out.Num() > 0;
}
