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

void CMapProgress::ResetState()
{
	m_vPath.clear();
	m_HasStart = false;
	m_HasGoal = false;
	m_PathValid = false;
	m_PathAttempted = false;
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

	m_Path.Init(Collision(), 5);
	CollectTileCandidates();
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

bool CMapProgress::BuildPathIfNeeded(const vec2 &Pos)
{
	if(m_PathValid)
		return true;

	if(m_PathAttempted)
		return false;

	if(!m_Path.IsInitialized())
		return false;

	if(m_vStartCandidates.empty() || m_vFinishCandidates.empty())
		return false;

	if(!m_HasStart)
	{
		m_StartPos = FindNearest(m_vStartCandidates, Pos);
		m_HasStart = m_StartPos.x >= 0.0f && m_StartPos.y >= 0.0f;
	}

	if(!m_HasGoal && m_HasStart)
	{
		m_GoalPos = FindNearest(m_vFinishCandidates, m_StartPos);
		m_HasGoal = m_GoalPos.x >= 0.0f && m_GoalPos.y >= 0.0f;
	}

	if(!m_HasStart || !m_HasGoal)
		return false;

	if(!m_Path.BuildPath(m_StartPos, m_GoalPos, m_vPath))
	{
		m_PathAttempted = true;
		return false;
	}

	m_TotalLength = PathLength(m_vPath);
	m_PathValid = m_TotalLength > 0.001f;
	m_PathAttempted = true;
	if(!m_PathValid)
		m_vPath.clear();

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
		for(size_t i = 1; i < m_vPath.size(); i++)
		{
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
	if(!m_Path.SamplePathMetrics(m_vPath, Pos, Progress, Dist, Dir))
		return;

	if(m_TotalLength <= 0.0f)
		return;

	const float Ratio = std::clamp(Progress / m_TotalLength, 0.0f, 1.0f);

	const float ScreenW = 300.0f * Graphics()->ScreenAspect();
	const float ScreenH = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);

	const float BarW = std::clamp((float)g_Config.m_ClMapProgressWidth, 1.0f, ScreenW);
	const float BarH = std::clamp((float)g_Config.m_ClMapProgressHeight, 1.0f, ScreenH);
	const float PosX = std::clamp((float)g_Config.m_ClMapProgressX, 0.0f, ScreenW - BarW);
	const float PosY = std::clamp((float)g_Config.m_ClMapProgressY, 0.0f, ScreenH - BarH);

	const float Rounding = std::min(std::clamp(BarH * 0.35f, 2.0f, 6.0f), BarW * 0.5f);

	Graphics()->DrawRect(PosX, PosY, BarW, BarH, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, Rounding);

	if(Ratio > 0.0f)
	{
		const float FillW = BarW * Ratio;
		const float FillRounding = std::min(Rounding, FillW * 0.5f);
		const int Corners = Ratio >= 0.999f ? IGraphics::CORNER_ALL : IGraphics::CORNER_L;
		const ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMapProgressColor));
		Graphics()->DrawRect(PosX, PosY, FillW, BarH, Col, Corners, FillRounding);
	}

	{
		char aBuf[16];
		const int Percent = (int)std::round(Ratio * 100.0f);
		str_format(aBuf, sizeof(aBuf), "%d%%", Percent);

		const float FontSize = std::clamp(BarH * 0.9f, 5.0f, 12.0f);
		const float TextWidth = TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f);

		float TextX = PosX + BarW + 4.0f;
		if(TextX + TextWidth > ScreenW)
			TextX = std::max(0.0f, PosX - 4.0f - TextWidth);
		const float TextY = PosY + (BarH - FontSize) * 0.5f;

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->Text(TextX, TextY, FontSize, aBuf, -1.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}
