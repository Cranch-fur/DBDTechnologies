#pragma once
#include "Utilities.h"






/*
	r.Upscaler.TargetFPS drives the engine's own dynamic resolution (r.DynamicRes.*): the engine measures
	GPU time and changes the screen percentage, the upscalers only have to accept a changing input size.
*/
class DynamicResolution
{
private:
	struct EngineVariables
	{
		void* operationMode = nullptr;
		void* frameTimeBudget = nullptr;
		void* minScreenPercentage = nullptr;
		void* maxScreenPercentage = nullptr;
	};

	static inline EngineVariables engine = {};
	static inline bool found = false;
	static inline volatile LONG active = 0;


public:
	/* After Console::Register */
	static void FindEngineVariables();

	/* Game thread, once per frame: engine variables cannot be changed from the render thread */
	static void Update();

	/* Enabled by r.Upscaler.TargetFPS or manually through r.DynamicRes.OperationMode */
	static bool IsActive() { return active != 0; }
	static const char* StatusText() { return active ? "  DYN" : ""; }
};
