#include <Forms/TESObjectCELL.h>
#include <TESObjectREFR.h>
#include <PlayerCharacter.h>

Vector<TESObjectREFR*> TESObjectCELL::GetRefsByFormTypes(const Vector<FormType>& aFormTypes) const noexcept
{
    Vector<TESObjectREFR*> references{};

    if (!refData.refArray)
        return references;

    for (uint32_t i = 0; i < refData.capacity; i++)
    {
        TESObjectREFR* pRef = refData.refArray[i].Get();
        if (!pRef)
            continue;

        for (FormType formType : aFormTypes)
        {
            if (pRef->baseForm->formType == formType)
                references.push_back(pRef);
        }
    }

    return references;
}

void TESObjectCELL::GetCOCPlacementInfo(NiPoint3* aOutPos, NiPoint3* aOutRot, bool aAllowCellLoad) noexcept
{
    TP_THIS_FUNCTION(TGetCOCPlacementInfo, void, TESObjectCELL, NiPoint3*, NiPoint3*, bool);
    POINTER_SKYRIMSE(TGetCOCPlacementInfo, s_getCOCPlacementInfo, 19075);
    TiltedPhoques::ThisCall(s_getCOCPlacementInfo, this, aOutPos, aOutRot, aAllowCellLoad);
}

namespace
{
// Anything at or above this is not a user mode pointer
constexpr uintptr_t kMaxUserPointer = 0x00007FFFFFFFFFFF;

bool IsPlausiblePointer(const void* apPointer) noexcept
{
    return reinterpret_cast<uintptr_t>(apPointer) <= kMaxUserPointer;
}

// A reference can end up with a parent cell that was freed and reused. Its memory stays mapped, so tell it
// apart from a real cell by what a real cell always has.
bool LooksLikeValidCell(const TESObjectCELL* apCell) noexcept
{
    if (apCell->formType != FormType::Cell)
        return false;

    if (!IsPlausiblePointer(apCell->loadedCellData) || !IsPlausiblePointer(apCell->worldspace))
        return false;

    // All cells share one vtable, compare with a cell that is known to be alive
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const TESObjectCELL* pPlayerCell = pPlayer ? pPlayer->parentCell : nullptr;
    if (pPlayerCell && pPlayerCell != apCell && *reinterpret_cast<void* const*>(pPlayerCell) != *reinterpret_cast<void* const*>(apCell))
        return false;

    return true;
}
} // namespace

TP_THIS_FUNCTION(TAddReference, void, TESObjectCELL, TESObjectREFR* apRefr);
static TAddReference* RealAddReference = nullptr;

// Adds a reference to the cell's reference list, it is called with the reference's parent cell when a save is
// loaded. A reference whose parent cell is no longer a cell crashed there.
void TP_MAKE_THISCALL(HookAddReference, TESObjectCELL, TESObjectREFR* apRefr)
{
    if (apRefr && apThis && apRefr->parentCell == apThis && !LooksLikeValidCell(apThis))
    {
        spdlog::error("Reference {:X} has an invalid parent cell {}, detaching it from the cell", apRefr->formID, static_cast<void*>(apThis));
        apRefr->parentCell = nullptr;
        return;
    }

    TiltedPhoques::ThisCall(RealAddReference, apThis, apRefr);
}

static TiltedPhoques::Initializer s_cellHooks(
    []()
    {
        POINTER_SKYRIMSE(TAddReference, s_addReference, 19032);

        RealAddReference = s_addReference.Get();

        TP_HOOK(&RealAddReference, HookAddReference);
    });
