#pragma once

#include "Engine/World.h"

class FEditorFileUtils
{
public:
	static bool NewScene(UWorld* World);
	static bool SaveScene(UWorld* World);
	static bool SaveSceneAs(UWorld* World);
	static bool LoadScene(UWorld* World);
	static FString ChooseFbxFile();
	// Scripts 폴더에서 시작하는 Lua 파일 선택 창. 작업 폴더 아래 파일이면 상대 경로("Scripts/foo.lua")를 돌려준다.
	static FString ChooseLuaScriptFile();

	static FString CurrentScenePath;
private:
	static FString OpenSaveSceneDialog();
	static FString OpenLoadSceneDialog();

};

