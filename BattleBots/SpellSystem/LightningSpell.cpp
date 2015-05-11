// // Copyright 2015 VMR Games, Inc. All Rights Reserved.

#include "BattleBots.h"
#include "LightningSpell.h"




float ALightningSpell::ProcessElementalDmg(float initialDamage)
{
  if (GetSpellCaster()) {
    float dmgMod = 1 + FMath::Clamp(GetSpellCaster()->GetDamageModifier_Lightning(), -1.f, 1.f);
    return FMath::Abs(initialDamage * dmgMod);
  }
  else {
    GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Caster is null - Lightning"));
    return initialDamage;
  }
}

FDamageEvent& ALightningSpell::GetDamageEvent()
{
  generalDamageEvent.DamageTypeClass = UBBotDmgType_Lightning::StaticClass();
  return generalDamageEvent;
}