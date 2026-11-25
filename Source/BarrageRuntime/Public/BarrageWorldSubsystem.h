#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BarrageSimulation.h"
#include "BarrageAssets.h"
#include "BarragePlayback.h"
#include "BarrageWorldSubsystem.generated.h"

class UInstancedStaticMeshComponent;

USTRUCT()
struct FBarrageMeshBatch
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UBarrageProjectileProfile> Profile = nullptr;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Component = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBarrageImpactDelegate, const FBarrageImpactEvent&, Impact);

// Game-thread service in Iteration 03. No per-projectile UObject or Actor.
UCLASS()
class BARRAGERUNTIME_API UBarrageWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Impact events are dispatched AFTER the simulation step, never during a dense-array loop.
    UPROPERTY(BlueprintAssignable, Category="Barrage|Collision") FBarrageImpactDelegate OnProjectileImpact;

    UFUNCTION(BlueprintCallable, Category="Barrage|Rendering")
    void SetMeshRenderingEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category="Barrage|Simulation")
    FBarrageProjectileHandle SpawnProjectile(const FBarrageSpawnParams& Params);

    UFUNCTION(BlueprintCallable, Category="Barrage|Simulation")
    FBarrageProjectileHandle SpawnProfile(UBarrageProjectileProfile* Profile, FVector WorldPosition, FVector WorldDirection,
        AActor* SourceActor = nullptr, bool bIgnoreSourceForCollision = true);

    // Returns the number of accepted emissions. Timed patterns continue automatically during Tick.
    UFUNCTION(BlueprintCallable, Category="Barrage|Patterns")
    int32 SpawnPattern(UBarragePattern* Pattern, FTransform SpawnTransform);

    // Optional target snapshot is shared by directed/aimed layers, including rotated planes.
    UFUNCTION(BlueprintCallable, Category="Barrage|Patterns")
    int32 PlayPattern(UBarragePattern* Pattern, FTransform SpawnTransform, bool bHasTarget, FVector TargetWorldPosition,
        AActor* SourceActor = nullptr, bool bIgnoreSourceForCollision = true);

    UFUNCTION(BlueprintCallable, Category="Barrage|Sequences")
    int32 PlaySequence(UBarrageSequence* Sequence, FTransform SpawnTransform, bool bHasTarget, FVector TargetWorldPosition,
        AActor* SourceActor = nullptr, bool bIgnoreSourceForCollision = true);

    // Tracked playback lets one emitter stop its own future shots without canceling others.
    // A handle may become inactive naturally after its final scheduled shot fires.
    UFUNCTION(BlueprintCallable, Category="Barrage|Patterns")
    FBarragePlaybackHandle StartPatternTracked(UBarragePattern* Pattern, FTransform SpawnTransform,
        bool bHasTarget, FVector TargetWorldPosition, AActor* SourceActor = nullptr,
        bool bIgnoreSourceForCollision = true);

    UFUNCTION(BlueprintCallable, Category="Barrage|Sequences")
    FBarragePlaybackHandle StartSequenceTracked(UBarrageSequence* Sequence, FTransform SpawnTransform,
        bool bHasTarget, FVector TargetWorldPosition, AActor* SourceActor = nullptr,
        bool bIgnoreSourceForCollision = true);

    UFUNCTION(BlueprintCallable, Category="Barrage|Patterns")
    bool CancelPlayback(FBarragePlaybackHandle Handle);

    UFUNCTION(BlueprintPure, Category="Barrage|Patterns")
    bool IsPlaybackActive(FBarragePlaybackHandle Handle) const;

    // Global stop is retained for callers intentionally canceling every scheduled attack.
    UFUNCTION(BlueprintCallable, Category="Barrage|Patterns")
    void CancelScheduledPatterns();

    UFUNCTION(BlueprintPure, Category="Barrage|Patterns")
    int32 GetActivePatternCount() const { return ActivePlaybacks.Num(); }

    UFUNCTION(BlueprintCallable, Category="Barrage|Simulation")
    bool DestroyProjectile(FBarrageProjectileHandle Handle);

    UFUNCTION(BlueprintPure, Category="Barrage|Simulation")
    bool GetProjectileSnapshot(FBarrageProjectileHandle Handle, FBarrageProjectileSnapshot& OutSnapshot) const;

    UFUNCTION(BlueprintPure, Category="Barrage|Simulation")
    int32 GetProjectileCount() const { return Simulation.Num(); }

    UFUNCTION(BlueprintPure, Category="Barrage|Diagnostics")
    FBarrageFrameStats GetFrameStats() const { return LastFrameStats; }

    // Resets counters without changing live projectile or playback state.
    UFUNCTION(BlueprintCallable, Category="Barrage|Diagnostics")
    void ResetFrameStats() { LastFrameStats = FBarrageFrameStats(); }

    UFUNCTION(BlueprintCallable, Category="Barrage|Simulation")
    void ClearProjectiles();

    // Native external adapters (including optional Orbit integration) can install a
    // provider on the Game Thread. Never call arbitrary UObject providers from workers.
    void SetGravitySampler(FBarrageSimulation::FGravitySampler&& Sampler) { GravitySampler = MoveTemp(Sampler); }
    void ClearGravitySampler() { GravitySampler = nullptr; }

    UPROPERTY(EditAnywhere, Category="Barrage|Simulation", meta=(ClampMin="1.0", ClampMax="480.0"))
    float FixedStepHz = 60.0f;
    UPROPERTY(EditAnywhere, Category="Barrage|Simulation", meta=(ClampMin="1", ClampMax="32"))
    int32 MaxStepsPerTick = 8;
    UPROPERTY(EditAnywhere, Category="Barrage|Rendering")
    bool bEnableMeshRendering = true;
    UPROPERTY(EditAnywhere, Category="Barrage|Debug")
    bool bDrawDebugProjectiles = false;
    UPROPERTY(EditAnywhere, Category="Barrage|Debug", meta=(ClampMin="0", ClampMax="2000"))
    int32 MaxDebugProjectiles = 256;
private:
    // UPROPERTY traversal keeps referenced Projectile Profiles alive while shots are queued.
    UPROPERTY(Transient) TArray<FBarrageActivePlayback> ActivePlaybacks;
    void AdvancePlaybacks(double DeltaSeconds);
    void ProcessProjectileEvents(const TArray<FBarrageImpactRecord>& Impacts,
        const TArray<FBarrageRemovalRecord>& Removals);
    void ObserveSpawn(FBarrageActivePlayback& Playback, const FBarrageScheduledShot& Shot,
        const FBarrageProjectileHandle& Handle, const FVector& Position);
    void EnqueueEventShots(FBarrageActivePlayback& Playback, int32 RuleIndex, uint64& FiredRules,
        const FVector& Origin);
    FBarrageProjectileHandle SpawnPlaybackShot(FBarrageActivePlayback& Playback,
        const FBarrageScheduledShot& Shot, const FVector& Position, const FVector& AnchorOffset);
    TArray<FBarrageRemovalRecord> PendingExplicitRemovals;
    int32 QueuePlayback(FBarrageActivePlayback&& Playback, AActor* SourceActor,
        bool bIgnoreSourceForCollision, FBarragePlaybackHandle* OutHandle);
    int32 NextPlaybackId = 1;
    FBarrageFrameStats LastFrameStats;
    FBarrageProjectileHandle SpawnWithProfile(UBarrageProjectileProfile* Profile, FBarrageSpawnParams Params);
    int32 FindOrAddRenderStyle(UBarrageProjectileProfile* Profile);
    void UpdateMeshBatches();
    UPROPERTY(Transient) TArray<FBarrageMeshBatch> MeshBatches;
    TArray<TArray<FTransform>> MeshTransformScratch;
    // Last transforms submitted to ISM, in render-style order. Unchanged batches
    // retain their GPU buffers (important for Stationary and paused formations).
    TArray<TArray<FTransform>> LastSubmittedMeshTransforms;
    double PerfLogElapsedSeconds = 0.0;
    double PerfFrameCpuSumMs = 0.0;
    double PerfSimulationCpuSumMs = 0.0;
    double PerfSubmissionCpuSumMs = 0.0;
    double PerfEventCpuSumMs = 0.0;
    double PerfPlaybackCpuSumMs = 0.0;
    int32 PerfPeakProjectiles = 0;
    int32 PerfPeakObserved = 0;
    int32 PerfPeakPending = 0;
    int32 PerfLogSamples = 0;
    FBarrageSimulation Simulation;
    FBarrageSimulation::FGravitySampler GravitySampler;
    double AccumulatorSeconds = 0.0;
};
