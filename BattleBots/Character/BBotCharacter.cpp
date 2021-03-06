// Copyright 2015 VMR Games, Inc. All Rights Reserved.

#include "BattleBots.h"
#include "BBotCharacter.h"
#include "Online/BBotsPlayerState.h"
#include "BattleBotsGameMode.h"
#include "SpellSystem/SpellSystem.h"
#include "SpellSystem/DamageTypes/BBotDmgType_Holy.h"
#include "SpellSystem/DamageTypes/BBotDmgType_Fire.h"
#include "SpellSystem/DamageTypes/BBotDmgType_Ice.h"
#include "SpellSystem/DamageTypes/BBotDmgType_Lightning.h"
#include "SpellSystem/DamageTypes/BBotDmgType_Poison.h"
#include "SpellSystem/DamageTypes/BBotDmgType_Physical.h"


#define SPELL_BAR_SIZE 6

// Sets default values
ABBotCharacter::ABBotCharacter(const FObjectInitializer& ObjectInitializer)
  :Super(ObjectInitializer)
{
  // Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
  PrimaryActorTick.bCanEverTick = true;

  //Must be true for an Actor to replicate anything
  bReplicates = true;
  bReplicateMovement = true;
  bAlwaysRelevant = true;

  UCharacterMovementComponent* MoveComp = GetCharacterMovement();
  // Adjust jump to make it less floaty
  MoveComp->GravityScale = 1.5f;
  MoveComp->JumpZVelocity = 620;

  // Calls our custom collision handler
  GetCapsuleComponent()->OnComponentBeginOverlap.AddDynamic(this, &ABBotCharacter::OnCollisionOverlap);

  //Initialize the size of the spell bar
  //spellBar_Internal.SetNum(SPELL_BAR_SIZE, false);

  // The combat stance index
  stanceIndex = 0;
}

// Called after all components have been initialized with default values
void ABBotCharacter::PostInitializeComponents()
{
  Super::PostInitializeComponents();

  if (HasAuthority())
  {
    // Sets max health/oil to the default values on the server
    maxHealth = health;
    maxOil = oil;
    GetCharacterMovement()->MaxWalkSpeed = characterConfig.movementSpeed;

    EnableSpellCasting(true);

    // Set the player controller that is possessing this pawn
    playerController = (Controller != NULL) ? Cast<ABattleBotsPlayerController>(Controller) : Cast<ABattleBotsPlayerController>(GetOwner());

    //Init the 3 stances dependant on archetype - mage, warrior, etc
    InitCombatStances();
  }
}

// Called when the game starts or when spawned
void ABBotCharacter::BeginPlay()
{
  Super::BeginPlay();
  GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, TEXT("Spawning arch char"));

  // Is called to ensure that the default stance is triggered on spawn
  OnRep_StanceChanged();
}

// Called every frame
void ABBotCharacter::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);

  if (GetCharacterMovement()->Velocity.SizeSquared() > 5 && !bCanCastWhileMoving)
  {
    // Stop casting the spell while the character is moving
    GetWorldTimerManager().ClearTimer(castingSpellHandler);
  }
}

// Called to bind functionality to input
void ABBotCharacter::SetupPlayerInputComponent(class UInputComponent* InputComponent)
{
  Super::SetupPlayerInputComponent(InputComponent);
  //InputComponent->BindAction("CastSpellOnRightClick", IE_Pressed, this, &ABBotCharacter::CastOnRightClick);
  InputComponent->BindAction("Jump", IE_Pressed, this, &ABBotCharacter::OnJumpStart);
  InputComponent->BindAction("Jump", IE_Released, this, &ABBotCharacter::OnJumpEnd);
  InputComponent->BindAction("ScrollUp", IE_Pressed, this, &ABBotCharacter::OnScrollUp);
  InputComponent->BindAction("ScrollDown", IE_Pressed, this, &ABBotCharacter::OnScrollDown);
}

// Is called when the player collides with something
void ABBotCharacter::OnCollisionOverlap(class AActor* OtherActor, class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{

}

float ABBotCharacter::GetCurrentHealth() const
{
  return health;
}

float ABBotCharacter::GetMaxHealth() const
{
  return maxHealth;
}

float ABBotCharacter::GetCurrentOil() const
{
  return oil;
}

// Pass in pos # to inc, or neg to decrement from current oil
void ABBotCharacter::SetCurrentOil(float decOil)
{
  if (HasAuthority()) {
    oil = FMath::Clamp(oil + decOil, 0.f, GetMaxOil());
  }
}

float ABBotCharacter::GetMaxOil() const
{
  return maxOil;
}

bool ABBotCharacter::IsAlive() const
{
  return health > 0.f;
}


bool ABBotCharacter::CanRecieveDamage(AController* damageInstigator, const TSubclassOf<UDamageType> DamageType) const
{
  if (HasAuthority())
  {
    ABattleBotsGameMode* GM = GetWorld()->GetAuthGameMode<ABattleBotsGameMode>();
    if (GM)
      return GM->CanDealDamage(damageInstigator, Controller);
  }
  return false;
}


bool ABBotCharacter::ShouldTakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) const
{
  return Super::ShouldTakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser)
    && CanRecieveDamage(EventInstigator, DamageEvent.DamageTypeClass);
}

// Take damage and handle death
float ABBotCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
  if (health <= 0.f) {
    return 0.f;
  }

  const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
  // Process damage post resist
  float DamageToApply = ProcessDamageTypes(ActualDamage, DamageEvent);

  if (DamageToApply > 0.f) {
    health -= DamageToApply;

    GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("I took damage") + FString::FromInt(DamageToApply) + TEXT(" MY HEALTH: ") + FString::FromInt(GetCurrentHealth()));

    if (health <= 0) {
      Die(DamageToApply, DamageEvent, EventInstigator, DamageCauser);
    }
    else {
      // @todo: play hit animation
    }
  }

  return DamageToApply;
}

float ABBotCharacter::ProcessDamageTypes(float Damage, struct FDamageEvent const& DamageEvent)
{
  if (DamageEvent.DamageTypeClass == UBBotDmgType_Physical::StaticClass()) {
    return ProcessFinalDmgPostResist(Damage, characterConfig.physicalResist);
  }
  else if (DamageEvent.DamageTypeClass == UBBotDmgType_Ice::StaticClass()) {
    return ProcessFinalDmgPostResist(Damage, characterConfig.iceResist);
  }
  else if (DamageEvent.DamageTypeClass == UBBotDmgType_Lightning::StaticClass()) {
    return ProcessFinalDmgPostResist(Damage, characterConfig.lightningResist);
  }
  else if (DamageEvent.DamageTypeClass == UBBotDmgType_Holy::StaticClass()) {
    return ProcessFinalDmgPostResist(Damage, characterConfig.holyResist);
  }
  else if (DamageEvent.DamageTypeClass == UBBotDmgType_Poison::StaticClass()) {
    return ProcessFinalDmgPostResist(Damage, characterConfig.poisonResist);
  }
  else if (DamageEvent.DamageTypeClass == UBBotDmgType_Fire::StaticClass()) {
    return ProcessFinalDmgPostResist(Damage, characterConfig.fireResist);
  }
  else {
    return Damage;
  }
}


float ABBotCharacter::ProcessFinalDmgPostResist(float initialDmg, float currentResist)
{
  // If the resist is negative, then apply additional damage
  float resistModifier = 1 - FMath::Clamp(currentResist, -1.f, 1.f);
  return FMath::Abs(initialDmg * resistModifier);
}

// Set the spell damage by x%
void ABBotCharacter::SetDamageModifier_All(float newDmgMod)
{
  // Must be overriden
}

void ABBotCharacter::UpdateMovementSpeed()
{
  if (HasAuthority())
  {
    float speedMod = (1 + FMath::Clamp(GetMoveSpeedMod(), -1.f, 1.f));
    float newSpeed = GetDefaultCharConfigValues().movementSpeed * speedMod;
    characterConfig.movementSpeed = newSpeed;
    GetCharacterMovement()->MaxWalkSpeed = FMath::Clamp(characterConfig.movementSpeed, minMovementSpeed, maxMovementSpeed);
    GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("Max Walk speed is: ") + FString::FromInt(GetCharacterMovement()->MaxWalkSpeed));
  }
}

//@todo: might be better to move the slow timers to character instead of trying to manage multiple timers
void ABBotCharacter::SlowPlayer(float slowMod, ASpellSystem* slowedBy)
{
  if (HasAuthority())
  {
	  // Only update if the new slow percentage is greater than the 1 in effect
	  if (slowMod <= characterConfig.movSpeedMod_spells)
	  {
	    /* Update the current slowed by spell, since we just got hit by a more 
	    powerful slow effect and must clear the timers of the previous spell.*/
	    if (currentSlowSpell)
	    {
	      currentSlowSpell->ClearUniqueTimers();	      
	    }
	    
	    currentSlowSpell = slowedBy;
	
	    // Only 1 spell slow is active at a time.
		  characterConfig.movSpeedMod_spells = slowMod;
	    UpdateMovementSpeed();
	  }
    else
    {
      /* There is already a more powerful spell in place. This prevents the 
      weaker spell from clearing the slow effect of the stronger spell.*/
      slowedBy->ClearUniqueTimers();
    }
  }
}


void ABBotCharacter::ClearSlow()
{
  if (HasAuthority())
  {
    characterConfig.movSpeedMod_spells = 0;
    UpdateMovementSpeed();
  }
}


// Set the character movSpeedMod_stance by x%
void ABBotCharacter::SetMobilityModifier_All(float newSpeedMod)
{
  if (HasAuthority())
  {
    characterConfig.movSpeedMod_stance = newSpeedMod;
    UpdateMovementSpeed();
  }
}

void ABBotCharacter::SetDefenseModifier_All(float newDefenseMod)
{
  if (HasAuthority())
  {
	  stanceResistMod = newDefenseMod;
	  UpdatePlayerResist();
	  //Must be overriden with Super for classes with block rating
  }
}

void ABBotCharacter::ReducePlayerResist(float reduceBy, const TSubclassOf<UDamageType> DamageType, bool bReduceAllResist /*= false*/)
{
  if (HasAuthority())
  {
    if (bReduceAllResist)
      SetResistAll(reduceBy);

    if (!bReduceAllResist)
    {
	    if (DamageType == UBBotDmgType_Physical::StaticClass()) {
	      spellBuffDebuffConfig.physicalResist += reduceBy;
	    }
	    else if (DamageType == UBBotDmgType_Ice::StaticClass()) {
	      spellBuffDebuffConfig.iceResist += reduceBy;
	    }
	    else if (DamageType == UBBotDmgType_Lightning::StaticClass()) {
	      spellBuffDebuffConfig.lightningResist += reduceBy;
	    }
	    else if (DamageType == UBBotDmgType_Holy::StaticClass()) {
	      spellBuffDebuffConfig.holyResist += reduceBy;
	    }
	    else if (DamageType == UBBotDmgType_Poison::StaticClass()) {
	      spellBuffDebuffConfig.poisonResist += reduceBy;
	    }
	    else if (DamageType == UBBotDmgType_Fire::StaticClass()) {
	      spellBuffDebuffConfig.fireResist += reduceBy;
	    }
    }

    // Update Character Resist
    UpdatePlayerResist();
  }
}

void ABBotCharacter::UpdatePlayerResist()
{
  if (HasAuthority())
  {
    characterConfig.fireResist = FMath::Clamp(GetDefaultCharConfigValues().fireResist + stanceResistMod + spellBuffDebuffConfig.fireResist, -1.f, 1.f);
    characterConfig.iceResist = FMath::Clamp(GetDefaultCharConfigValues().iceResist + stanceResistMod + spellBuffDebuffConfig.iceResist, -1.f, 1.f);
    characterConfig.lightningResist = FMath::Clamp(GetDefaultCharConfigValues().lightningResist + stanceResistMod + spellBuffDebuffConfig.lightningResist, -1.f, 1.f);
    characterConfig.poisonResist = FMath::Clamp(GetDefaultCharConfigValues().poisonResist + stanceResistMod + spellBuffDebuffConfig.poisonResist, -1.f, 1.f);
    characterConfig.physicalResist = FMath::Clamp(GetDefaultCharConfigValues().physicalResist + stanceResistMod + spellBuffDebuffConfig.physicalResist, -1.f, 1.f);
    characterConfig.holyResist = FMath::Clamp(GetDefaultCharConfigValues().holyResist + stanceResistMod + spellBuffDebuffConfig.holyResist, -1.f, 1.f);
  }
}

void ABBotCharacter::SetResistAll(float newResistanceMod)
{
  if (HasAuthority())
  {
    //@TODO: A bit repetitive, maybe create a stance struct
    spellBuffDebuffConfig.fireResist += FMath::Clamp(newResistanceMod, -1.f, 1.f);
    spellBuffDebuffConfig.iceResist += FMath::Clamp(newResistanceMod, -1.f, 1.f);
    spellBuffDebuffConfig.lightningResist += FMath::Clamp(newResistanceMod, -1.f, 1.f);
    spellBuffDebuffConfig.poisonResist += FMath::Clamp(newResistanceMod, -1.f, 1.f);
    spellBuffDebuffConfig.physicalResist += FMath::Clamp(newResistanceMod, -1.f, 1.f);
    spellBuffDebuffConfig.holyResist += FMath::Clamp(newResistanceMod, -1.f, 1.f);
  }
}

bool ABBotCharacter::CanDie(float killingDamage, FDamageEvent const& DamageEvent, AController* killer, AActor* damageCauser)
{
  if (IsDying()										// already dying
    || IsPendingKill()								// already destroyed
    || Role != ROLE_Authority						// not authority
    || GetWorld()->GetAuthGameMode() == NULL
    || GetWorld()->GetAuthGameMode()->GetMatchState() == MatchState::LeavingMap)	// level transition occurring
  {
    return false;
  }

  return true;
}

bool ABBotCharacter::Die(float killingDamage, FDamageEvent const& DamageEvent, AController* killer, AActor* damageCauser)
{
  if (!CanDie(killingDamage, DamageEvent, killer, damageCauser))
  {
    return false;
  }

  AController* const KilledPlayer = (Controller != NULL) ? Controller : Cast<AController>(GetOwner());
  GetWorld()->GetAuthGameMode<ABattleBotsGameMode>()->Killed(killer, KilledPlayer, this, DamageEvent.DamageTypeClass);

  OnDeath(killingDamage, DamageEvent, killer ? killer->GetPawn() : NULL, damageCauser);

  return true;
}

void ABBotCharacter::OnDeath_Implementation(float killingDamage, FDamageEvent const& DamageEvent, APawn* pawnInstigator, AActor* damageCauser)
{
  if (IsDying())
  {
    return;
  }

  bReplicateMovement = false;
  bTearOff = true;
  SetIsDying(true);

  //@TODO: Fix role authority, maybe adjust collision under authority, and ragdoll on multicast
  //   if (Role == ROLE_Authority)
  //   {
  // Play death sound
  UGameplayStatics::PlaySoundAtLocation(this, deathSound, GetActorLocation());

  DetachFromControllerPendingDestroy();

  // disable collisions on capsule
  GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);

  if (GetMesh())
  {
    static FName CollisionProfileName(TEXT("Ragdoll"));
    GetMesh()->SetCollisionProfileName(CollisionProfileName);
  }
  SetActorEnableCollision(true);

  // Returns the death anim's duration
  float deathAnimDuration = PlayAnimMontage(deathAnim);

  // Ragdoll after the death animation has played
  if (deathAnimDuration > 0.f)
  {
    // Use a local timer handle as we don't need to store it for later and we don't need to look for something to clear
    FTimerHandle ragdollTimerHandle;
    GetWorldTimerManager().SetTimer(ragdollTimerHandle, this, &ABBotCharacter::SetRagdollPhysics, FMath::Min(0.1f, deathAnimDuration), false);
  }
  else
  {
    SetRagdollPhysics();
  }
  //}
}

void ABBotCharacter::SetRagdollPhysics_Implementation()
{
  bool bInRagdoll = false;

  if (IsPendingKill())
  {
    bInRagdoll = false;
  }
  else if (!GetMesh() || !GetMesh()->GetPhysicsAsset())
  {
    bInRagdoll = false;
  }
  else
  {
    // initialize physics/etc
    GetMesh()->SetAllBodiesSimulatePhysics(true);
    GetMesh()->SetSimulatePhysics(true);
    GetMesh()->WakeAllRigidBodies();
    GetMesh()->bBlendPhysics = true;

    bInRagdoll = true;
  }

  GetCharacterMovement()->StopMovementImmediately();
  GetCharacterMovement()->DisableMovement();
  GetCharacterMovement()->SetComponentTickEnabled(false);

  if (!bInRagdoll)
  {
    // hide and set short lifespan
    TurnOff();
    SetActorHiddenInGame(true);
    SetLifeSpan(1.0f);
  }
  else
  {
    SetLifeSpan(10.0f);
  }
}

// Function called on right mouse click
void ABBotCharacter::CastOnRightClick()
{
  //@todo: currently handled under PC, but might be 
  //best to move it back here to disable specific input for stun.

  // Rotate to the direction of our mouse click
  RotateToMouseCursor();
  // Cast spell from our dedicated spell bar index
  //CastFromSpellBar(0);
}

bool ABBotCharacter::CanCast(int32 spellIndex)
{
  // If the character is currently dying prevent casting.
  if (IsSpellCastingEnabled()
    && !GetIsStunned()
    && spellBar.IsValidIndex(spellIndex)
    && spellBar[spellIndex]->IsValidLowLevel()
    && !spellBar[spellIndex]->SpellOnCD()
    && !IsGlobalCDActive()
    && !GetWorldTimerManager().IsTimerActive(castingSpellHandler)
    && !IsDying())
  {
    spellCost = spellBar[spellIndex]->GetSpellCost();
    return 0 <= (GetCurrentOil() - spellCost);
  }

  return false;
}

// Casts the spell at index
void ABBotCharacter::CastFromSpellBar(int32 index, const FVector& HitLocation)
{
  if (Role < ROLE_Authority) {
    // We short-circuit if we can cast to prevent unnecessary calls
    ServerCastFromSpellBar(index, HitLocation);
  }
  else {
    if (!IsGlobalCDActive()) {

      if (CanCast(index)) {

        // If the cast time is 0, change it to 0.01f to prevent an infinite wait with the timer
        float castTime = spellBar[index]->GetCastTime() == 0.f ? 0.01f : spellBar[index]->GetCastTime();

        bCanCastWhileMoving = spellBar[index]->CastableWhileMoving();

        // Set the spellSpawnLocation to prevent re-binding our FTimerDelegate
        spellBar[index]->SetSpellSpawnLocation(HitLocation);

        // Attach a spellBar index payLoad to the delegate
        castingSpellDelegate.BindUObject(this, &ABBotCharacter::CastFromSpellBar_Internal, (int32)index);

        // Cast the spell after cast time in seconds
        GetWorldTimerManager().SetTimer(castingSpellHandler, castingSpellDelegate, castTime, false);

        SetCurrentOil(-spellCost);
        GCDHelper = GetWorld()->GetTimeSeconds() + characterConfig.globalCooldown;
      }
    }
  }
}

void ABBotCharacter::ServerCastFromSpellBar_Implementation(int32 index, const FVector& HitLocation)
{
  CastFromSpellBar(index, HitLocation);
}

bool ABBotCharacter::ServerCastFromSpellBar_Validate(int32 index, const FVector& HitLocation)
{
  return true;
}


void ABBotCharacter::CastFromSpellBar_Internal(int32 index)
{
  if (HasAuthority())
  {
    spellBar[index]->SpawnSpell(spellBar_Internal[index]);
  }
}


// Add a new spell to the spell bar
void ABBotCharacter::AddSpellToBar(TSubclassOf<ASpellSystem> newSpell)
{
  if (Role < ROLE_Authority)
  {
    ServerAddSpellToBar(newSpell);
  }
  else
  {
    if (spellBar_Internal.Num() <= SPELL_BAR_SIZE) {

      spellBar_Internal.Add(newSpell);

      FActorSpawnParameters spawnInfo;
      spawnInfo.Owner = this;
      spawnInfo.Instigator = this;
      spawnInfo.bNoCollisionFail = true;

      ASpellSystem* spellManager = GetWorld()->SpawnActor<ASpellSystem>(newSpell, spawnInfo);


      if (spellManager)
      {
        /* Set the spellManagers' collision and hidden in game to true.
        * Prevents them from getting GC'd */
        spellManager->SetActorEnableCollision(false);
        spellManager->SetActorHiddenInGame(true);
        // Add the spellManager to the spellBar
        spellBar.Add(spellManager);
        GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Adding spell"));
      }
    }
  }
}

void ABBotCharacter::ServerAddSpellToBar_Implementation(TSubclassOf<ASpellSystem> newSpell)
{
  AddSpellToBar(newSpell);
}

bool ABBotCharacter::ServerAddSpellToBar_Validate(TSubclassOf<ASpellSystem> newSpell)
{
  return true;
}

// Rotate player to mouse click location
void ABBotCharacter::RotateToMouseCursor()
{
  ABBotCharacter* const playerCharacter = this;
  if (playerCharacter && playerController) {
    // Get hit location under mouse click
    FHitResult hit;
    playerController->GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, hit);

    FVector mouseHitLoc = hit.Location;
    FVector characterLoc = playerCharacter->GetActorLocation();

    // Get the target location direction
    FVector targetLoc = (mouseHitLoc - characterLoc);
    targetLoc.Normalize();

    DrawDebugLine(GetWorld(), characterLoc, mouseHitLoc, FColor::Green, true, 0.5f, 0, 2.f);
    FRotator newRotation(0.f, targetLoc.Rotation().Yaw, 0.f);

    // Stop current movement,and face the new direction
    playerController->StopMovement();
    //playerCharacter->SetActorRotation(newRotation);
    playerCharacter->ChangeFacingRotation(newRotation);
  }
}


void ABBotCharacter::ChangeFacingRotation(FRotator newRotation)
{
  this->SetActorRotation(newRotation);

  if (Role < ROLE_Authority) {
    ServerChangeFacingRotation(newRotation);
  }
}

void ABBotCharacter::ServerChangeFacingRotation_Implementation(FRotator newRotation)
{
  ChangeFacingRotation(newRotation);
}

bool ABBotCharacter::ServerChangeFacingRotation_Validate(FRotator newRotation)
{
  return true;
}

void ABBotCharacter::OnJumpStart()
{
  bPressedJump = true;
}

void ABBotCharacter::OnJumpEnd()
{
  bPressedJump = false;
}

bool ABBotCharacter::GetIsStunned() const
{
  return bIsStunned;
}

void ABBotCharacter::SetIsStunned(bool stunned)
{
  if (HasAuthority()) {
    bIsStunned = stunned;
  }
}

void ABBotCharacter::OnRep_IsStunned()
{
  this->DisableInput(playerController);
  // @todo: stop character movement while stunned
}

void ABBotCharacter::InitCombatStances()
{
  // Must be overriden
}

void ABBotCharacter::printCurrentStance()
{
  // Must override
}

void ABBotCharacter::OnRep_StanceChanged()
{
  // Must be overriden
}

void ABBotCharacter::ServerOnRep_StanceChanged_Implementation()
{
  OnRep_StanceChanged();
}

bool ABBotCharacter::ServerOnRep_StanceChanged_Validate()
{
  return true;
}

void ABBotCharacter::SetToMobilityStance()
{
  // Must be overriden
}

void ABBotCharacter::SetToDamageStance()
{
  // Must be overriden
}

void ABBotCharacter::SetToDefenseStance()
{
  // Must be overriden
}

// Getter for the current stance.
EStanceType ABBotCharacter::GetCurrentStance() const
{
  return currentStance;
}
// Setter for the current stance.
void ABBotCharacter::SetCurrentStance(EStanceType newStance)
{
  if (HasAuthority()) {
    currentStance = newStance;
  }
}

// Called on mouse wheel up
void ABBotCharacter::OnScrollUp()
{
  SwitchCombatStanceHelper(true);
  GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Scrolling UP"));
}

// Called on mouse wheel down
void ABBotCharacter::OnScrollDown()
{
  SwitchCombatStanceHelper(false);
  GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("Scrolling Down"));
}

void ABBotCharacter::SwitchCombatStanceHelper(bool bScrolled)
{
  if (Role < ROLE_Authority)
  {
    ServerSwitchCombatStanceHelper(bScrolled);
  }
  else
  {
    float currentTime = GetWorld()->GetTimeSeconds();
    // If the switchStance cd is up, call SwitchCombatStance
    if (switchStanceCDHelper < currentTime) {
      // Setting this to true increments the stance index
      bScrolledUp = bScrolled;
      SwitchCombatStance();
      // Reapply cooldown after switching stance
      switchStanceCDHelper = currentTime + switchStanceCoolDown;
    }
  }
}

void ABBotCharacter::ServerSwitchCombatStanceHelper_Implementation(bool bScrolled)
{
  SwitchCombatStanceHelper(bScrolled);
}

bool ABBotCharacter::ServerSwitchCombatStanceHelper_Validate(bool bScrolled)
{
  return true;
}

void ABBotCharacter::SwitchCombatStance()
{
  if (HasAuthority())
  {
    stanceIndex += bScrolledUp ? 1 : -1;
    int32 roundRobinIndex = FMath::Abs(stanceIndex) % combatStances.Num();

    if (combatStances.IsValidIndex(roundRobinIndex)) {
      //currentStance = combatStances[roundRobinIndex];
      SetCurrentStance(combatStances[roundRobinIndex]);
    }
    printCurrentStance();
  }
}


void ABBotCharacter::KnockbackPlayer(FVector spellPosition)
{
  if (HasAuthority())
  {
	  // @Todo: Launches character at a random direction, possible due to the spell location being too close
	  FVector kbDirection = GetActorLocation() - spellPosition;
	  FVector launchForce = (FVector)(kbDirection.Normalize() * 300); // Kb ammount
	  LaunchCharacter(launchForce, false, true);
  }
}

void ABBotCharacter::SwitchTeams()
{
  ABBotsPlayerState* myState = Cast<ABBotsPlayerState>(PlayerState);

  if (myState)
  {
    if (myState->GetTeamNum() == 0)
    {
      myState->SetTeamNum(1);
    }
    else{
      myState->SetTeamNum(0);
    }
  }
}

void ABBotCharacter::TurnOff()
{
  //Disable spell casting
  EnableSpellCasting(false);

  if (GetMesh())
  {
    GetMesh()->TickAnimation(2.0f);
    GetMesh()->RefreshBoneTransforms();
  }

  Super::TurnOff();
}

void ABBotCharacter::EnableSpellCasting(bool bCanCast)
{
  ServerEnableSpellCasting(bCanCast);
}

void ABBotCharacter::ServerEnableSpellCasting_Implementation(bool bCanCast)
{
  this->bCastingEnabled = bCanCast;
}

bool ABBotCharacter::ServerEnableSpellCasting_Validate(bool bCanCast)
{
  return true;
}

void ABBotCharacter::ServerSetIsDying_Implementation(bool bDying)
{
  bIsDying = bDying;
}

bool ABBotCharacter::ServerSetIsDying_Validate(bool bDying)
{
  return true;
}

void ABBotCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // Value is already updated locally, so we may skip it in replication step for the owner only
  //DOREPLIFETIME_CONDITION(ABBotCharacter, X, COND_SkipOwner);

  // Value is only relevant to owner
  DOREPLIFETIME_CONDITION(ABBotCharacter, maxHealth, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ABBotCharacter, maxOil, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ABBotCharacter, bIsDying, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ABBotCharacter, GCDHelper, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ABBotCharacter, spellBar, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ABBotCharacter, spellBar_Internal, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ABBotCharacter, spellCost, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ABBotCharacter, bCastingEnabled, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ABBotCharacter, characterConfig, COND_OwnerOnly);

  // Replicate to every client, no special condition required
  DOREPLIFETIME(ABBotCharacter, health);
  DOREPLIFETIME(ABBotCharacter, oil);
  DOREPLIFETIME(ABBotCharacter, bIsStunned);
  DOREPLIFETIME(ABBotCharacter, currentStance);
  DOREPLIFETIME(ABBotCharacter, combatStances);
}




