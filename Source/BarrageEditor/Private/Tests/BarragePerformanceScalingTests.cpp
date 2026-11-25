#include "Misc/AutomationTest.h"
#include "BarrageSimulation.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace BarragePerfScale
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FBarrageSimulation::FGravitySampler NoProvider = [](const FVector&) { return FVector::ZeroVector; };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBarrageSimulationScalingMatrix,
    "Barrage.Performance.SimulationScaling1000To10000", BarragePerfScale::Flags)
bool FBarrageSimulationScalingMatrix::RunTest(const FString&)
{
    // Informational timings, NOT a fragile wall-clock pass/fail threshold.
    // CPU core only: no UWorld, physics scene, mesh uploads, or GPU work.
    for (int32 Count : { 1000, 5000, 10000 })
    {
        for (int32 Mode=0; Mode<3; ++Mode)
        {
            FBarrageSimulation Sim;
            FBarrageSpawnParams P;
            P.Velocity = FVector(250, 0, 0);
            P.Lifetime = 10.0f;
            P.GravityMode = Mode == 1 ? EBarrageGravityMode::Constant : EBarrageGravityMode::None;
            P.ConstantAcceleration = FVector(0, 0, -200);
            if (Mode == 2)
            {
                P.MotionMode = EBarrageMotionMode::CurvedFlight;
                P.OrbitAxis = FVector::UpVector;
                P.OrbitDegreesPerSecond = 90.0f;
            }
            for (int32 I=0; I<Count; ++I)
            {
                P.Position = FVector(I*0.5, I*0.1, I*0.05);
                if (!Sim.Spawn(P).IsSet()) return false;
            }
            const double Start = FPlatformTime::Seconds();
            for (int32 Step=0; Step<60; ++Step)
                Sim.Step(1.0f/60.0f, FVector::ZeroVector, BarragePerfScale::NoProvider);
            const double Ms = (FPlatformTime::Seconds()-Start)*1000.0;
            AddInfo(FString::Printf(TEXT("CPU core %d shots x 60 steps, mode=%s: %.3f ms total (%.4f ms/step). No threshold; excludes World, rendering, collision and GPU."),
                Count, Mode==0?TEXT("Free"):Mode==1?TEXT("ConstantGravity"):TEXT("Curved"), Ms, Ms/60.0));
            TestEqual(TEXT("All benchmark projectiles remain alive"), Sim.Num(), Count);
        }
    }
    return true;
}
#endif
