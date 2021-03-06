// Copyright 2015 VMR Games, Inc. All Rights Reserved.

#pragma once

#include "Character/BBotCharacter.h"
#include "BBotSorcerer.generated.h"

/**
 *
 */
UCLASS()
class BATTLEBOTS_API ABBotSorcerer : public ABBotCharacter
{
  GENERATED_BODY()

protected:

  // Initializes the class relevant stances
  virtual void InitCombatStances() override;

  // Called on stance change
  virtual void OnRep_StanceChanged() override;

  virtual void SetToMobilityStance() override;

  virtual void SetToDamageStance() override;
  
  virtual void SetToDefenseStance() override;

  virtual void SetDamageModifier_All(float newDmgMod) override;

  // Prints a debug msg of the current stance.
  virtual void printCurrentStance() override;
};
