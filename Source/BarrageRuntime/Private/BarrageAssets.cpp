#include "BarrageAssets.h"

FBarragePatternLayer UBarragePattern::CreateDefaultLayer(const FString& Name)
{
    FBarragePatternLayer Layer;
    Layer.LayerId = FGuid::NewGuid();
    Layer.Name = Name;
    Layer.Settings = FBarragePatternLibrary::MakeComposable(Layer.Settings);
    Layer.Settings.Preset = EBarragePatternPreset::RadialRing;
    Layer.Settings.Formation = EBarrageFormation::Ring;
    Layer.Settings.Modifier = EBarrageFormationModifier::None;
    Layer.Settings.Emission = EBarrageEmissionMode::Instant;
    Layer.Settings.bMultipleDirections = false;
    Layer.Settings.bNaturalGroups = false;
    FBarrageFormationBlock Generator;
    Generator.BlockId = FGuid::NewGuid();
    Generator.Kind = EBarrageFormationBlockKind::Generator;
    Generator.Geometry = Layer.Settings;
    Layer.FormationBlocks.Add(MoveTemp(Generator));
    FBarrageEmissionBlock Source;
    Source.BlockId = FGuid::NewGuid();
    Source.Kind = EBarrageEmissionBlockKind::Instant;
    Layer.EmissionBlocks.Add(MoveTemp(Source));
    FBarrageActivationBlock Trigger;
    Trigger.BlockId = FGuid::NewGuid();
    Layer.ActivationBlocks.Add(Trigger);
    FBarrageMotionBlock Movement;
    Movement.BlockId = FGuid::NewGuid();
    Movement.Movement = EBarrageMovementMode::Profile;
    Layer.MotionBlocks.Add(Movement);
    return Layer;
}

void UBarragePattern::InitializeNewPattern()
{
    if (Layers.IsEmpty()) Layers.Add(CreateDefaultLayer());
}

int32 UBarragePattern::InsertTemplate(const UBarragePattern* Template, int32 At)
{
    if (!Template || At < 0 || At > Layers.Num() ||
        Template->Layers.IsEmpty() || Layers.Num() + Template->Layers.Num() > 256) return 0;
    TArray<FBarragePatternLayer> Copies = Template->Layers;
    TMap<FGuid, FGuid> Remap;
    for (FBarragePatternLayer& Layer : Copies)
    {
        if (!Layer.LayerId.IsValid() || Remap.Contains(Layer.LayerId)) return 0;
        const FGuid NewId = FGuid::NewGuid();
        Remap.Add(Layer.LayerId, NewId);
        Layer.LayerId = NewId;
        if (!Layer.Projectile) Layer.Projectile = Template->Projectile;
        for (FBarrageFormationBlock& Block : Layer.FormationBlocks)
            Block.BlockId = FGuid::NewGuid();
        for (FBarrageEmissionBlock& Block : Layer.EmissionBlocks)
            Block.BlockId = FGuid::NewGuid();
        for (FBarrageActivationBlock& Block : Layer.ActivationBlocks)
            Block.BlockId = FGuid::NewGuid();
        for (FBarrageMotionBlock& Block : Layer.MotionBlocks)
            Block.BlockId = FGuid::NewGuid();
    }
    for (FBarragePatternLayer& Layer : Copies)
    {
        for (FBarrageActivationBlock& Block : Layer.ActivationBlocks)
        {
            if (Block.Kind != EBarrageActivationBlockKind::Trigger ||
                Block.Trigger == EBarrageLayerActivation::PatternStart) continue;
            const FGuid* Remapped = Remap.Find(Block.SourceLayerId);
            if (!Remapped) return 0;
            Block.SourceLayerId = *Remapped;
        }
        // Old scalar fields have no authority over the authored Activation Blocks.
        Layer.SourceLayerId.Invalidate();
    }
    for (int32 Index = 0; Index < Copies.Num(); ++Index)
        Layers.Insert(MoveTemp(Copies[Index]), At + Index);
    return Copies.Num();
}
