#include "map_progress.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
enum
{
	MAP_PROGRESS_DISPLAY_BAR = 0,
	MAP_PROGRESS_DISPLAY_PERCENT,
	MAP_PROGRESS_DISPLAY_BOTH,
};

bool MapProgressShowBar(int DisplayMode)
{
	return DisplayMode != MAP_PROGRESS_DISPLAY_PERCENT;
}

bool MapProgressShowPercent(int DisplayMode)
{
	return DisplayMode != MAP_PROGRESS_DISPLAY_BAR;
}
}

void CMapProgress::ResetState()
{
	m_vPath.clear();
	m_vPathTele.clear();
	m_HasStart = false;
	m_HasGoal = false;
	m_PathValid = false;
	m_PathAttempted = false;
	m_PathInitialized = false;
	m_CandidatesCollected = false;
	m_TotalLength = 0.0f;
	m_StartPos = vec2(0.0f, 0.0f);
	m_GoalPos = vec2(0.0f, 0.0f);
}

void CMapProgress::OnMapLoad()
{
	ResetState();
	m_vStartCandidates.clear();
	m_vFinishCandidates.clear();

	if(!Collision())
		return;
}

void CMapProgress::OnReset()
{
	ResetState();
}

void CMapProgress::OnStateChange(int NewState, int OldState)
{
	if(NewState == OldState)
		return;

	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_CONNECTING)
	{
		ResetState();
		m_vStartCandidates.clear();
		m_vFinishCandidates.clear();
		m_Path.Init(nullptr);
	}
}

void CMapProgress::CollectTileCandidates()
{
	const CCollision *pCollision = Collision();
	if(!pCollision)
		return;

	const int Width = pCollision->GetWidth();
	const int Height = pCollision->GetHeight();
	if(Width <= 0 || Height <= 0)
		return;

	const int MapSize = Width * Height;
	m_vStartCandidates.reserve(64);
	m_vFinishCandidates.reserve(64);

	for(int Index = 0; Index < MapSize; Index++)
	{
		const int Tile = pCollision->GetTileIndex(Index);
		const int Front = pCollision->GetFrontTileIndex(Index);
		if(Tile == TILE_START || Front == TILE_START)
			m_vStartCandidates.push_back(pCollision->GetPos(Index));
		if(Tile == TILE_FINISH || Front == TILE_FINISH)
			m_vFinishCandidates.push_back(pCollision->GetPos(Index));
	}
}

vec2 CMapProgress::FindNearest(const std::vector<vec2> &vCandidates, const vec2 &Pos)
{
	if(vCandidates.empty())
		return vec2(-1.0f, -1.0f);

	float BestDist = std::numeric_limits<float>::infinity();
	vec2 Best = vec2(-1.0f, -1.0f);
	for(const vec2 &P : vCandidates)
	{
		const float Dist = distance(P, Pos);
		if(Dist < BestDist)
		{
			BestDist = Dist;
			Best = P;
		}
	}
	return Best;
}

float CMapProgress::PathLength(const std::vector<vec2> &vPath)
{
	float Len = 0.0f;
	for(size_t i = 1; i < vPath.size(); i++)
	{
		Len += distance(vPath[i - 1], vPath[i]);
	}
	return Len;
}

float CMapProgress::PathLength(const std::vector<vec2> &vPath, const std::vector<unsigned char> &vTele)
{
	const bool UseTele = vTele.size() + 1 == vPath.size();
	float Len = 0.0f;
	for(size_t i = 1; i < vPath.size(); i++)
	{
		if(UseTele && vTele[i - 1])
			continue;
		Len += distance(vPath[i - 1], vPath[i]);
	}
	return Len;
}

bool CMapProgress::BuildPathIfNeeded(const vec2 &Pos)
{
	if(m_PathValid)
		return true;

	if(m_PathAttempted)
		return false;

	if(!m_PathInitialized)
	{
		if(!Collision())
			return false;

		m_Path.Init(Collision(), 5);
		m_PathInitialized = m_Path.IsInitialized();
	}

	if(!m_PathInitialized)
		return false;

	if(!m_CandidatesCollected)
	{
		CollectTileCandidates();
		m_CandidatesCollected = true;
	}

	if(m_vStartCandidates.empty() || m_vFinishCandidates.empty())
		return false;

	std::vector<int> vStartOrder(m_vStartCandidates.size());
	std::iota(vStartOrder.begin(), vStartOrder.end(), 0);
	std::stable_sort(vStartOrder.begin(), vStartOrder.end(), [&](int Left, int Right) {
		return distance(m_vStartCandidates[Left], Pos) < distance(m_vStartCandidates[Right], Pos);
	});

	auto TryBuild = [&](bool AllowFreeze) {
		std::vector<vec2> vBestPath;
		std::vector<unsigned char> vBestTele;
		vec2 BestStart(-1.0f, -1.0f);
		vec2 BestGoal(-1.0f, -1.0f);
		float BestLength = std::numeric_limits<float>::infinity();
		int BestTeleCount = std::numeric_limits<int>::max();
		bool Found = false;

		for(int StartIndex : vStartOrder)
		{
			const vec2 &StartCandidate = m_vStartCandidates[StartIndex];

			std::vector<int> vFinishOrder(m_vFinishCandidates.size());
			std::iota(vFinishOrder.begin(), vFinishOrder.end(), 0);
			std::stable_sort(vFinishOrder.begin(), vFinishOrder.end(), [&](int Left, int Right) {
				return distance(m_vFinishCandidates[Left], StartCandidate) < distance(m_vFinishCandidates[Right], StartCandidate);
			});

			for(int FinishIndex : vFinishOrder)
			{
				const vec2 &FinishCandidate = m_vFinishCandidates[FinishIndex];

				std::vector<vec2> vCandidatePath;
				std::vector<unsigned char> vCandidateTele;
				if(!m_Path.BuildPath(StartCandidate, FinishCandidate, vCandidatePath, vCandidateTele, AllowFreeze))
					continue;

				const float Length = PathLength(vCandidatePath, vCandidateTele);
				if(Length <= 0.001f)
					continue;

				const int TeleCount = (int)std::count(vCandidateTele.begin(), vCandidateTele.end(), (unsigned char)1);
				const bool Better =
					!Found ||
					Length < BestLength - 0.001f ||
					(std::abs(Length - BestLength) <= 0.001f && TeleCount < BestTeleCount);

				if(!Better)
					continue;

				Found = true;
				BestLength = Length;
				BestTeleCount = TeleCount;
				BestStart = StartCandidate;
				BestGoal = FinishCandidate;
				vBestPath = std::move(vCandidatePath);
				vBestTele = std::move(vCandidateTele);
			}
		}

		if(!Found)
			return false;

		m_StartPos = BestStart;
		m_GoalPos = BestGoal;
		m_vPath = std::move(vBestPath);
		m_vPathTele = std::move(vBestTele);
		m_TotalLength = BestLength;
		m_HasStart = true;
		m_HasGoal = true;
		return true;
	};

	if(!TryBuild(false) && !TryBuild(true))
	{
		m_PathAttempted = true;
		return false;
	}

	m_PathValid = m_TotalLength > 0.001f;
	m_PathAttempted = true;
	if(!m_PathValid)
	{
		m_vPath.clear();
		m_vPathTele.clear();
	}

	return m_PathValid;
}

bool CMapProgress::GetTargetPos(vec2 &OutPos) const
{
	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return false;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	if(!GameClient()->m_Snap.m_pLocalCharacter)
		return false;

	const int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0)
		return false;

	const auto &CharInfo = GameClient()->m_Snap.m_aCharacters[ClientId];
	if(!CharInfo.m_Active)
		return false;

	const float Intra = Client()->IntraGameTick(g_Config.m_ClDummy);
	OutPos = mix(vec2(CharInfo.m_Prev.m_X, CharInfo.m_Prev.m_Y), vec2(CharInfo.m_Cur.m_X, CharInfo.m_Cur.m_Y), Intra);
	return true;
}

void CMapProgress::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!g_Config.m_ClMapProgress && !g_Config.m_ClMapProgressDebug)
		return;

	vec2 Pos;
	if(!GetTargetPos(Pos))
		return;

	if(!BuildPathIfNeeded(Pos))
		return;

	if(g_Config.m_ClMapProgressDebug && m_vPath.size() >= 2)
	{
		const ColorRGBA PathCol = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMapProgressColor)).WithAlpha(0.6f);
		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		Graphics()->SetColor(PathCol);
		const bool UseTele = m_vPathTele.size() + 1 == m_vPath.size();
		for(size_t i = 1; i < m_vPath.size(); i++)
		{
			if(UseTele && m_vPathTele[i - 1])
				continue;
			const IGraphics::CLineItem Line(m_vPath[i - 1].x, m_vPath[i - 1].y, m_vPath[i].x, m_vPath[i].y);
			Graphics()->LinesDraw(&Line, 1);
		}
		Graphics()->LinesEnd();
	}

	if(!g_Config.m_ClMapProgress)
		return;

	float Progress = 0.0f;
	float Dist = 0.0f;
	vec2 Dir(0.0f, 0.0f);
	if(!m_Path.SamplePathMetrics(m_vPath, m_vPathTele, Pos, Progress, Dist, Dir))
		return;

	if(m_TotalLength <= 0.0f)
		return;

	const float Ratio = std::clamp(Progress / m_TotalLength, 0.0f, 1.0f);
	const int DisplayMode = std::clamp<int>(g_Config.m_ClMapProgressDisplay, MAP_PROGRESS_DISPLAY_BAR, MAP_PROGRESS_DISPLAY_BOTH);
	const bool ShowBar = MapProgressShowBar(DisplayMode);
	const bool ShowPercent = MapProgressShowPercent(DisplayMode);
	const bool Vertical = g_Config.m_ClMapProgressVertical != 0;

	const float ScreenW = 300.0f * Graphics()->ScreenAspect();
	const float ScreenH = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);

	const float BaseBarW = std::clamp((float)g_Config.m_ClMapProgressWidth, 1.0f, ScreenW);
	const float BaseBarH = std::clamp((float)g_Config.m_ClMapProgressHeight, 1.0f, ScreenH);
	const float BarW = Vertical ? std::min(BaseBarH, ScreenW) : BaseBarW;
	const float BarH = Vertical ? std::min(BaseBarW, ScreenH) : BaseBarH;
	const float PosXPct = std::clamp((float)g_Config.m_ClMapProgressX / 100.0f, 0.0f, 1.0f);
	const float PosYPct = std::clamp((float)g_Config.m_ClMapProgressY / 100.0f, 0.0f, 1.0f);
	const float PosX = (ScreenW - BarW) * PosXPct;
	const float PosY = (ScreenH - BarH) * PosYPct;
	const float MinorAxis = Vertical ? BarW : BarH;
	const float Rounding = std::min(std::clamp(MinorAxis * 0.35f, 2.0f, 6.0f), std::min(BarW, BarH) * 0.5f);

	if(ShowBar)
	{
		Graphics()->DrawRect(PosX, PosY, BarW, BarH, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, Rounding);

		if(Ratio > 0.0f)
		{
			const float FillInset = std::min(1.0f, MinorAxis * 0.18f);
			const float InnerW = std::max(0.0f, BarW - FillInset * 2.0f);
			const float InnerH = std::max(0.0f, BarH - FillInset * 2.0f);
			const float InnerRounding = std::min(std::max(0.0f, Rounding - FillInset), std::min(InnerW, InnerH) * 0.5f);
			const float FillPrimary = (Vertical ? InnerH : InnerW) * Ratio;

			if(FillPrimary > 0.0f && InnerW > 0.0f && InnerH > 0.0f)
			{
				const float FillRounding = std::min(InnerRounding, std::min(Vertical ? InnerW : FillPrimary, Vertical ? FillPrimary : InnerH) * 0.5f);
				const bool RoundAll = Ratio >= 0.999f || FillPrimary <= InnerRounding * 2.0f;
				const int Corners = RoundAll ? IGraphics::CORNER_ALL : (Vertical ? IGraphics::CORNER_B : IGraphics::CORNER_L);
				const ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMapProgressColor));

				if(Vertical)
					Graphics()->DrawRect(PosX + FillInset, PosY + FillInset + InnerH - FillPrimary, InnerW, FillPrimary, Col, Corners, FillRounding);
				else
					Graphics()->DrawRect(PosX + FillInset, PosY + FillInset, FillPrimary, InnerH, Col, Corners, FillRounding);
			}
		}
	}

	if(!ShowPercent)
		return;

	{
		char aBuf[16];
		const int Percent = (int)std::round(Ratio * 100.0f);
		str_format(aBuf, sizeof(aBuf), "%d%%", Percent);

		const float FontSize = std::clamp((ShowBar ? MinorAxis : std::min(BarW, BarH)) * 0.9f, 5.0f, 12.0f);
		const float TextWidth = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);

		float TextX = PosX;
		float TextY = PosY;
		if(!ShowBar)
		{
			TextX = PosX + (BarW - TextWidth) * 0.5f;
			TextY = PosY + (BarH - FontSize) * 0.5f;
		}
		else if(Vertical)
		{
			TextX = std::clamp(PosX + (BarW - TextWidth) * 0.5f, 0.0f, std::max(0.0f, ScreenW - TextWidth));
			TextY = PosY - FontSize - 4.0f;
			if(TextY < 0.0f)
				TextY = std::min(ScreenH - FontSize, PosY + BarH + 4.0f);
		}
		else
		{
			TextX = PosX + BarW + 4.0f;
			if(TextX + TextWidth > ScreenW)
				TextX = std::max(0.0f, PosX - 4.0f - TextWidth);
			TextY = PosY + (BarH - FontSize) * 0.5f;
		}

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->Text(TextX, TextY, FontSize, aBuf, -1.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}
