// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BattleBots : ModuleRules
{
	public BattleBots(TargetInfo Target)
	{
        PublicDependencyModuleNames.AddRange(new string[] { "AIModule", "Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore", "OnlineSubsystem"});
	}
}
