#include "d2r.h"

namespace d2r {

Api api {};

void Bind(uint64_t exeBase) noexcept {
	api.getSkillRecord = reinterpret_cast<GetSkillRecordFn>(exeBase + RvaGetSkillRecord);
	api.evalSkillCalc = reinterpret_cast<EvalSkillCalcFn>(exeBase + RvaEvalSkillCalc);
	api.skillTargetXY = reinterpret_cast<SkillTargetXYFn>(exeBase + RvaSkillTargetXY);
	api.createMissile = reinterpret_cast<CreateMissileFn>(exeBase + RvaCreateMissile);
	api.missileCount = reinterpret_cast<MissileCountFn>(exeBase + RvaMissileCount);
	api.unitSeed = reinterpret_cast<UnitSeedFn>(exeBase + RvaUnitSeed);
	api.setUnitSkillFlag = reinterpret_cast<SetUnitSkillFlagFn>(exeBase + RvaSetUnitSkillFlag);
	api.summonPet = reinterpret_cast<SummonPetFn>(exeBase + RvaSummonPet);
	api.setPetLevel = reinterpret_cast<SetPetLevelFn>(exeBase + RvaSetPetLevel);
	api.petSkillStats = reinterpret_cast<PetSkillStatsFn>(exeBase + RvaPetSkillStats);
	api.addPetToParty = reinterpret_cast<AddPetToPartyFn>(exeBase + RvaAddPetToParty);
}

}  // namespace d2r
