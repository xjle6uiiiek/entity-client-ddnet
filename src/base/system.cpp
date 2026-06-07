#include "system.h"

#include "color.h"
#include "str.h"

#include <engine/shared/config.h>

void str_to_uppercase(const char *src, char *dst, int dst_size)
{
	int i = 0;
	for(; src[i] && i < dst_size - 1; ++i)
		dst[i] = str_uppercase(src[i]);
	dst[i] = '\0';
}

const char *str_to_uppercase(const char *src)
{
	int len = str_length(src);
	char *dst = (char *)malloc(len + 1);
	if(!dst)
		return nullptr;
	for(int i = 0; i < len; ++i)
		dst[i] = str_uppercase(src[i]);
	dst[len] = '\0';
	return dst;
}

bool str_isalluppercase(const char *str)
{
	for(; *str; ++str)
	{
		// Only check alphabetic characters
		if(*str >= 'a' && *str <= 'z')
			return false;
	}
	return true;
}

void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

//ColorRGBA NightShiftColor(ColorRGBA OriginalColor)
//{
//	auto IsNightShiftEnabled = []() {
//		if(g_Config.m_ClNightShiftAlwaysOn)
//			return true;
//
//		if(!g_Config.m_ClNightShiftScheduled)
//			return false;
//
//		int StartHour = g_Config.m_ClNightShiftStart;
//		int EndHour = g_Config.m_ClNightShiftEnd;
//
//		using namespace std::chrono;
//		auto Now = system_clock::now();
//		std::time_t t = system_clock::to_time_t(Now);
//		std::tm Tm{};
//#if defined(_WIN32)
//		const errno_t Err = localtime_s(&Tm, &t);
//		if(Err != 0)
//			return false;
//#else
//		if(localtime_r(&t, &Tm) == nullptr)
//			return false;
//#endif
//
//		int CurrentHour = Tm.tm_hour;
//		if(StartHour < EndHour)
//			return CurrentHour >= StartHour && CurrentHour < EndHour;
//		else
//			return CurrentHour >= StartHour || CurrentHour < EndHour;
//	};
//
//	if(!IsNightShiftEnabled())
//		return OriginalColor;
//
//	const float Temperature = g_Config.m_ClNightShiftTemperature * 0.01f;
//
//	float R = 1.0f;
//	float G = 1.0f - Temperature * 0.15f;
//	float B = 1.0f - Temperature * 0.50f;
//
//	return OriginalColor.Multiply(ColorRGBA(R, G, B, 1.0f));
//}
