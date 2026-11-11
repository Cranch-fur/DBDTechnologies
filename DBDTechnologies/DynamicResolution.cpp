#include "DynamicResolution.h"
#include "Console.h"






void DynamicResolution::FindEngineVariables()
{
	engine.operationMode = Console::FindEngineVariable(L"r.DynamicRes.OperationMode");
	engine.frameTimeBudget = Console::FindEngineVariable(L"r.DynamicRes.FrameTimeBudget");
	engine.minScreenPercentage = Console::FindEngineVariable(L"r.DynamicRes.MinScreenPercentage");
	engine.maxScreenPercentage = Console::FindEngineVariable(L"r.DynamicRes.MaxScreenPercentage");
	found = engine.operationMode && engine.frameTimeBudget && engine.minScreenPercentage && engine.maxScreenPercentage;
	Utilities::Log::Write("r.DynamicRes.* %s", found ? "found" : "NOT found (r.Upscaler.TargetFPS unavailable)");
}

void DynamicResolution::Update()
{
	if (Console::IsReady() == false)
		return;

	const Settings& settings = Console::settings;
	int32_t fps = settings.targetFps;
	int32_t minimum = settings.minResolution;
	if (fps != 0 && (fps < 20 || fps > 500))
		fps = 0;
	if (minimum < 25) minimum = 25;
	if (minimum > 100) minimum = 100;

	static int32_t appliedFps = 0, appliedMinimum = -1;
	if (found && (fps != appliedFps || (fps != 0 && minimum != appliedMinimum)))
	{
		wchar_t text[32];
		if (fps != 0)
		{
			swprintf_s(text, L"%d", minimum);
			Console::SetEngineValue(engine.minScreenPercentage, text);
			Console::SetEngineValue(engine.maxScreenPercentage, L"100");
			swprintf_s(text, L"%.2f", 1000.0 / fps);
			Console::SetEngineValue(engine.frameTimeBudget, text);
			Console::SetEngineValue(engine.operationMode, L"2");   // 2 = always enabled
			Utilities::Log::Write("dynamic resolution on: target %d FPS (%.2f ms), %d%%..100%%", fps, 1000.0 / fps, minimum);
		}
		else
		{
			Console::SetEngineValue(engine.operationMode, L"0");
			Utilities::Log::Write("dynamic resolution off");
		}
		appliedFps = fps;
		appliedMinimum = minimum;
	}

	LONG enabled = engine.operationMode && Console::GetEngineInt(engine.operationMode, 0) != 0 ? 1 : 0;
	if (InterlockedExchange(&active, enabled) != enabled)
		Utilities::Log::Write("engine dynamic resolution: %s", enabled ? "active" : "off");
}
