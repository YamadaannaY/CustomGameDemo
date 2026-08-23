
#include "ExtractGameCharacter.h"

#include "GameplayDebugger.h"
#include "Modules/ModuleManager.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "ExtractGameCharacter/Debug/FCombatCameraDebuggerCategory.h"
#endif

/**
 * 自定义游戏模块：在启动时注册 CombatCamera 的 Gameplay Debugger 分类。
 */
class FExtractGameCharacterModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

#if WITH_GAMEPLAY_DEBUGGER
		IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
		GameplayDebuggerModule.RegisterCategory(
			"CombatCamera",
			IGameplayDebugger::FOnGetCategory::CreateStatic(&FCombatCameraDebuggerCategory::MakeInstance),
			EGameplayDebuggerCategoryState::EnabledInGameAndSimulate,
			0);
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_GAMEPLAY_DEBUGGER
		if (IGameplayDebugger::IsAvailable())
		{
			IGameplayDebugger::Get().UnregisterCategory("CombatCamera");
		}
#endif

		FDefaultGameModuleImpl::ShutdownModule();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE( FExtractGameCharacterModule, ExtractGameCharacter, "ExtractGameCharacter" );
