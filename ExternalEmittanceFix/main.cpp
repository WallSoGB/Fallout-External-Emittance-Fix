#include "nvse/PluginAPI.h"
#include <SafeWrite.h>
#include <GameData.h>
#include <fstream>
#include <string>

NVSEInterface* g_nvseInterface{};

bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "External Emittance Fix";
	info->version = 1;

	return true;
}

// TLDR
// Game resets the External Emittance color (Sunlight color of the weather) every time Player moves to a new region.
// Game doesn't check if new region shares the weather with the last one - it just forces color recalculation.
// Issue? Since there's no history of the previous weather, color blend calculations start from black, which obv paints every EE user dark for the first minute or so.
// This code aims to fix that by reusing current Sky weather if applicable, and keeping the data of the last used weather.

const TESWeather* pCurrentRegionWeather = nullptr;

TESWeather* __fastcall TESRegion_GetWeather(TESRegion* thiss) {
	if (thiss->weather) {
		pCurrentRegionWeather = thiss->weather;
		return thiss->weather;
	}
	// Pass current weather in case TESRegion has none - game will default to the... default weather otherwise.
	Sky* pSky = Sky::Get();
	if (pSky->pCurrentWeather) {
		if (!pCurrentRegionWeather) {
			pCurrentRegionWeather = pSky->pCurrentWeather;
		}
		return pSky->pCurrentWeather;
	}
	return nullptr;
}

void __fastcall Sky_FillColorBlendColors(Sky* thiss, void*, Sky::COLOR_BLEND* aColorBlend, const TESWeather* apCurrentWeather, const TESWeather* apLastWeather, int aeColorType, int* aeTime1, int* aeTime2) {
	const TESWeather* pCurrentNormalWeather = thiss->pCurrentWeather;
	const TESWeather* pLastNormalWeather = thiss->pLastWeather;

	// May seem like a duplicate from GetWeather, but it's to handle the case where world is first loaded in.
	// (There's no current weather in Sky or TESRegion, so game creates default one for TESRegion before calling this function.
	if (!pCurrentRegionWeather) {
		pCurrentRegionWeather = apCurrentWeather;
	}

	// Duplicate colors of the TESRegion weather itself in case next checks fail - prevents starting from black
	aColorBlend->uiRGBVal[2] = apCurrentWeather->uiColorData[4][*aeTime1];
	aColorBlend->uiRGBVal[3] = apCurrentWeather->uiColorData[4][*aeTime2];

	// Use blend colors from current Sky weather if it's shared with TESRegion
	if (pCurrentNormalWeather && (pCurrentRegionWeather == pCurrentNormalWeather)) {
		aColorBlend->uiRGBVal[2] = pCurrentNormalWeather->uiColorData[4][*aeTime1];
		aColorBlend->uiRGBVal[3] = pCurrentNormalWeather->uiColorData[4][*aeTime2];
	}
	// Use blend colors from the last known weather from the Sky if it happens to be reused
	else if (pLastNormalWeather && (pCurrentRegionWeather == pLastNormalWeather)) {
		aColorBlend->uiRGBVal[2] = pLastNormalWeather->uiColorData[4][*aeTime1];
		aColorBlend->uiRGBVal[3] = pLastNormalWeather->uiColorData[4][*aeTime2];
	}

	// Finally, set blend colors for TESRegion weather
	aColorBlend->uiRGBVal[0] = apCurrentWeather->uiColorData[4][*aeTime1];
	aColorBlend->uiRGBVal[1] = apCurrentWeather->uiColorData[4][*aeTime2];
}

void __fastcall Sky_SetColor(Sky* thiss, void*, NiColor* aColor, Sky::COLOR_BLEND* apColorBlend, float afAddFlash) {
	const TESWeather* pCurrentNormalWeather = thiss->pCurrentWeather;

	// Skip recalculating colors if we already have them used
	if (pCurrentNormalWeather && (pCurrentRegionWeather == pCurrentNormalWeather)) {
		aColor->r = thiss->pColors[4].r;
		aColor->g = thiss->pColors[4].g;
		aColor->b = thiss->pColors[4].b;
	}
	else {
		ThisStdCall(0x63C690, thiss, aColor, apColorBlend, afAddFlash); // Sky::SetColor
	}
}

bool NVSEPlugin_Load(NVSEInterface* nvse) {
	if (!nvse->isEditor) {
		WriteRelCall(0x551ECE, UInt32(TESRegion_GetWeather));
		WriteRelCall(0x55215C, UInt32(TESRegion_GetWeather));

		WriteRelCall(0x551F5F, UInt32(Sky_FillColorBlendColors));
		WriteRelCall(0x552205, UInt32(Sky_FillColorBlendColors));

		WriteRelCall(0x551FA8, UInt32(Sky_SetColor));
		WriteRelCall(0x552251, UInt32(Sky_SetColor));
	}

	return true;
}