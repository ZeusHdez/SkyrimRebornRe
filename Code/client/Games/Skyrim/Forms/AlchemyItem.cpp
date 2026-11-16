#include "AlchemyItem.h"

#include <Systems/ModSystem.h>
#include <Magic/EffectItem.h>
#include <World.h>
#include <Games/BGSCreatedObjectManager.h>

AlchemyItem* AlchemyItem::Create(const Vector<Inventory::EffectItem>& aEffects) noexcept
{
    // The first argument is a created object smart pointer, which is a single pointer
    TP_THIS_FUNCTION(TAddPotion, void, BGSCreatedObjectManager, AlchemyItem**, GameArray<EffectItem>*);
    POINTER_SKYRIMSE(TAddPotion, addPotion, 36167);

    if (aEffects.empty())
        return nullptr;

    ModSystem& modSystem = World::Get().GetModSystem();

    GameArray<EffectItem> effects{};
    effects.Resize(aEffects.size());
    for (int i = 0; i < aEffects.size(); i++)
    {
        const Inventory::EffectItem& effect = aEffects[i];

        EffectItem effectItem{};
        effectItem.data.fMagnitude = effect.Magnitude;
        effectItem.data.iArea = effect.Area;
        effectItem.data.iDuration = effect.Duration;
        effectItem.fRawCost = effect.RawCost;
        effectItem.pEffectSetting = Cast<EffectSetting>(TESForm::GetById(modSystem.GetGameId(effect.EffectId)));
        if (!effectItem.pEffectSetting)
        {
            spdlog::error("Effect setting not found: {:X}:{:X}", effect.EffectId.ModId, effect.EffectId.BaseId);
            return nullptr;
        }

        effects[i] = effectItem;
    }

    // The reference held by the smart pointer is never released, so the game keeps the created form
    AlchemyItem* pItem = nullptr;
    TiltedPhoques::ThisCall(addPotion, BGSCreatedObjectManager::Get(), &pItem, &effects);

    return pItem;
}
