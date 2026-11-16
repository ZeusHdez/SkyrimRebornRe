#pragma once

#include "MagicItem.h"

#include <Structs/Inventory.h>

struct AlchemyItem : MagicItem
{
    enum Flags : uint32_t
    {
        kPoison = 1 << 17,
    };

    // Creates a player-made potion or poison, like the alchemy workbench does
    static AlchemyItem* Create(const Vector<Inventory::EffectItem>& aEffects) noexcept;

    bool IsPoison() const noexcept { return (uiFlags & kPoison) != 0; }

    // TESModelTextureSwap, TESIcon, BGSMessageIcon, TESWeightForm, BGSEquipType, BGSDestructibleObjectForm, BGSPickupPutdownSounds
    uint8_t pad90[0x138 - 0x90];
    int32_t iCostOverride;
    uint32_t uiFlags;
    void* pAddictionItem;
    float fAddictionChance;
    uint32_t pad14C;
    void* pConsumptionSound;
    uint8_t pad158[0x168 - 0x158];
};

static_assert(offsetof(AlchemyItem, uiFlags) == 0x13C);
static_assert(sizeof(AlchemyItem) == 0x168);
