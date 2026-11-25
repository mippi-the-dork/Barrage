#include "BarragePatternParameterPolicy.h"
#include "BarrageAssets.h"

bool FBarragePatternParameterPolicy::IsRelevant(EBarragePatternPreset P, FName N)
{
    using E = EBarragePatternPreset;
    if (N == TEXT("Preset")) return true;
    if (N == TEXT("Count")) return P != E::Grid && P != E::WaveCurtain && P != E::Checkerboard;
    if (N == TEXT("Rows") || N == TEXT("Columns")) return P == E::Grid || P == E::WaveCurtain || P == E::Checkerboard;
    if (N == TEXT("Depth")) return P == E::Grid;
    if (N == TEXT("Arms")) return P == E::Star || P == E::MultiArmSpiral || P == E::Helix;
    if (N == TEXT("Waves")) return P == E::RotatingRing;
    if (N == TEXT("Radius")) return P == E::Arc || P == E::RadialRing || P == E::Star ||
        P == E::RotatingRing || P == E::Rose || P == E::OrbitalHalo || P == E::SafeGap ||
        P == E::SplitBloom || P == E::Sphere || P == E::Hemisphere || P == E::Helix;
    if (N == TEXT("InnerRadius")) return P == E::Star;
    if (N == TEXT("Width")) return P == E::Directed || P == E::Wall || P == E::Grid ||
        P == E::Scatter || P == E::Rain || P == E::WaveCurtain || P == E::Checkerboard || P == E::Pincer;
    if (N == TEXT("Height")) return P == E::Grid || P == E::Scatter || P == E::Rain || P == E::Checkerboard;
    if (N == TEXT("Length")) return P == E::Grid || P == E::Scatter || P == E::Rain || P == E::Pincer || P == E::Helix;
    if (N == TEXT("AngleOffsetDegrees")) return P != E::Pincer;
    if (N == TEXT("AngleSpanDegrees")) return P == E::Fan || P == E::Arc || P == E::Sweep;
    if (N == TEXT("Turns")) return P == E::Spiral || P == E::MultiArmSpiral || P == E::RotatingRing || P == E::Sweep || P == E::Helix;
    if (N == TEXT("AngularVelocityDegrees")) return P == E::OrbitalHalo;
    if (N == TEXT("IntervalSeconds")) return P == E::Burst || P == E::Rain || P == E::Spiral || P == E::MultiArmSpiral ||
        P == E::RotatingRing || P == E::Sweep || P == E::WaveCurtain || P == E::Checkerboard;
    if (N == TEXT("DelaySeconds")) return P == E::SplitBloom || P == E::OrbitalHalo;
    if (N == TEXT("Amplitude") || N == TEXT("Frequency")) return P == E::WaveCurtain;
    if (N == TEXT("Petals")) return P == E::Rose;
    if (N == TEXT("GapFraction")) return P == E::SafeGap;
    if (N == TEXT("ConeAngleDegrees")) return P == E::Cone || P == E::Hemisphere;
    if (N == TEXT("RandomSeed")) return P == E::Scatter || P == E::Rain;
    if (N == TEXT("bInward")) return P == E::Arc || P == E::RadialRing || P == E::Star || P == E::RotatingRing ||
        P == E::Rose || P == E::SafeGap || P == E::SplitBloom || P == E::Sphere || P == E::Hemisphere || P == E::Helix;
    if (N == TEXT("bVolume")) return P == E::Scatter || P == E::Rain || P == E::Cone;
    if (N == TEXT("bAimAtTarget")) return P != E::OrbitalHalo;
    return false;
}

const TCHAR* FBarragePatternParameterPolicy::Description(EBarragePatternPreset P)
{
    using E = EBarragePatternPreset;
    switch (P)
    {
    case E::Directed: return TEXT("Parallel shots along local X. Width distributes their origins along Y.");
    case E::Burst: return TEXT("Repeated shots from one point. Interval spaces them in time.");
    case E::Fan: return TEXT("Multiple directions spread from a single origin across an angle.");
    case E::Arc: return TEXT("Spawn points around an angular arc; direction follows the radius.");
    case E::RadialRing: return TEXT("Evenly spaced projectiles around a circle, inward or outward.");
    case E::Star: return TEXT("Radial spokes with Count projectiles per Arm.");
    case E::Wall: return TEXT("Evenly spaced parallel shots across a straight line.");
    case E::Grid: return TEXT("3D lattice defined by Rows, Columns and Depth.");
    case E::Scatter: return TEXT("Seeded random spawn positions in a 2D region or 3D volume.");
    case E::Rain: return TEXT("Time-staggered random positions in a region or volume.");
    case E::Spiral: return TEXT("Timed directions rotate through Turns revolutions.");
    case E::MultiArmSpiral: return TEXT("Timed spiral with several simultaneous arms.");
    case E::RotatingRing: return TEXT("Several time-separated rings with angular offsets.");
    case E::Sweep: return TEXT("Timed cosine sweep between two angular extremes.");
    case E::WaveCurtain: return TEXT("Rows of shots with a sine-wave height offset.");
    case E::Checkerboard: return TEXT("Alternating occupied positions in a timed grid.");
    case E::Rose: return TEXT("Exact Petals lobes, odd or even. Count is total projectiles distributed around the entire flower.");
    case E::OrbitalHalo: return TEXT("Orbit around the emitter; Delay controls time until tangential release (0 means never).");
    case E::SafeGap: return TEXT("Radial ring with a clear angular corridor.");
    case E::Pincer: return TEXT("Two lines on opposing X sides, aimed towards the center.");
    case E::SplitBloom: return TEXT("A parent projectile splits into Count children after Delay.");
    case E::Cone: return TEXT("Spherical cone of outgoing directions, either on the rim or inside.");
    case E::Sphere: return TEXT("Even 3D distribution around a spherical shell.");
    case E::Hemisphere: return TEXT("Spherical cap centered on local X.");
    case E::Helix: return TEXT("3D helical emission with Count points per Arm.");
    default: return TEXT("Configure projectile emission geometry.");
    }
}
