#include "debug_path.h"

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

std::size_t CDebugPath::CStateHash::operator()(const CProgState &State) const
{
	return (std::size_t)State.m_SubIndex * 1315423911u ^ (std::size_t)State.m_TeleCheckpoint;
}

CDebugPath::CDebugPath() :
	m_pCollision(nullptr),
	m_Width(0),
	m_Height(0),
	m_SubDiv(5),
	m_SubWidth(0),
	m_SubHeight(0),
	m_Initialized(false),
	m_AllowFreeze(false),
	m_HasAnyTeleCheckOut(false)
{
}

void CDebugPath::Init(CCollision *pCollision, int SubDiv)
{
	m_pCollision = pCollision;
	m_SubDiv = std::max(1, SubDiv);
	m_Width = m_pCollision ? m_pCollision->GetWidth() : 0;
	m_Height = m_pCollision ? m_pCollision->GetHeight() : 0;
	m_SubWidth = m_Width * m_SubDiv;
	m_SubHeight = m_Height * m_SubDiv;
	m_Initialized = false;
	m_AllowFreeze = false;
	m_HasAnyTeleCheckOut = false;

	m_vSubPassable.clear();
	m_vSubGrounded.clear();
	m_vSubFinish.clear();
	m_vSubFreeze.clear();
	m_vSubDistance.clear();
	m_vTeleType.clear();
	m_vTeleNumber.clear();
	m_vTeleIns.clear();
	m_vTeleOuts.clear();
	m_vTeleCheckIns.clear();
	m_vTeleCheckOuts.clear();

	if(!m_pCollision || m_Width <= 0 || m_Height <= 0)
		return;

	BuildTeleCache();
	BuildCache();
	m_Initialized = !m_vSubPassable.empty();
}

bool CDebugPath::IsInitialized() const
{
	return m_Initialized;
}

bool CDebugPath::SubCellInBounds(int X, int Y) const
{
	return X >= 0 && X < m_SubWidth && Y >= 0 && Y < m_SubHeight;
}

int CDebugPath::SubCellIndex(int X, int Y) const
{
	return Y * m_SubWidth + X;
}

vec2 CDebugPath::SubCellCenter(int X, int Y) const
{
	const float Size = 32.0f / (float)m_SubDiv;
	return vec2(X * Size + Size * 0.5f, Y * Size + Size * 0.5f);
}

bool CDebugPath::SubCellSafe(int X, int Y) const
{
	if(!SubCellInBounds(X, Y))
		return false;
	const int Index = SubCellIndex(X, Y);
	return Index >= 0 && Index < (int)m_vSubPassable.size() && m_vSubPassable[Index] != 0;
}

bool CDebugPath::SubCellGrounded(int X, int Y) const
{
	if(!SubCellInBounds(X, Y))
		return false;
	const int Index = SubCellIndex(X, Y);
	return Index >= 0 && Index < (int)m_vSubGrounded.size() && m_vSubGrounded[Index] != 0;
}

bool CDebugPath::SubCellFinish(int X, int Y) const
{
	if(!SubCellInBounds(X, Y))
		return false;
	const int Index = SubCellIndex(X, Y);
	return Index >= 0 && Index < (int)m_vSubFinish.size() && m_vSubFinish[Index] != 0;
}

void CDebugPath::CollectTouchedIndices(const vec2 &Pos, int (&aIndices)[5]) const
{
	for(int i = 0; i < 5; i++)
		aIndices[i] = -1;

	if(!m_pCollision)
		return;

	const float Hs = CCharacterCore::PhysicalSize() / 2.0f;

	aIndices[0] = m_pCollision->GetPureMapIndex(Pos);
	aIndices[1] = m_pCollision->GetPureMapIndex(vec2(Pos.x - Hs, Pos.y));
	aIndices[2] = m_pCollision->GetPureMapIndex(vec2(Pos.x + Hs, Pos.y));
	aIndices[3] = m_pCollision->GetPureMapIndex(vec2(Pos.x, Pos.y - Hs));
	aIndices[4] = m_pCollision->GetPureMapIndex(vec2(Pos.x, Pos.y + Hs));
}

bool CDebugPath::MapIndexDeath(int Index) const
{
	if(!m_pCollision || Index < 0)
		return false;

	const int Tile = m_pCollision->GetTileIndex(Index);
	const int Front = m_pCollision->GetFrontTileIndex(Index);
	return Tile == TILE_DEATH || Front == TILE_DEATH;
}

bool CDebugPath::MapIndexFreeze(int Index) const
{
	if(!m_pCollision || Index < 0)
		return false;

	const int Tile = m_pCollision->GetTileIndex(Index);
	const int Front = m_pCollision->GetFrontTileIndex(Index);

	return Tile == TILE_FREEZE || Front == TILE_FREEZE ||
	       Tile == TILE_DFREEZE || Front == TILE_DFREEZE ||
	       Tile == TILE_LFREEZE || Front == TILE_LFREEZE;
}

bool CDebugPath::MapIndexFinish(int Index) const
{
	if(!m_pCollision || Index < 0)
		return false;

	const int Tile = m_pCollision->GetTileIndex(Index);
	const int Front = m_pCollision->GetFrontTileIndex(Index);
	return Tile == TILE_FINISH || Front == TILE_FINISH;
}

bool CDebugPath::IsPosInDeath(const vec2 &Pos) const
{
	int aIndices[5];
	CollectTouchedIndices(Pos, aIndices);

	for(int i = 0; i < 5; i++)
	{
		if(MapIndexDeath(aIndices[i]))
			return true;
	}

	return false;
}

bool CDebugPath::IsPosInFreeze(const vec2 &Pos) const
{
	int aIndices[5];
	CollectTouchedIndices(Pos, aIndices);

	for(int i = 0; i < 5; i++)
	{
		if(MapIndexFreeze(aIndices[i]))
			return true;
	}

	return false;
}

bool CDebugPath::IsPosInFinish(const vec2 &Pos) const
{
	int aIndices[5];
	CollectTouchedIndices(Pos, aIndices);

	for(int i = 0; i < 5; i++)
	{
		if(MapIndexFinish(aIndices[i]))
			return true;
	}

	return false;
}

bool CDebugPath::IsPosSafe(const vec2 &Pos) const
{
	if(!m_pCollision)
		return false;

	if(m_pCollision->TestBox(Pos, CCharacterCore::PhysicalSizeVec2()))
		return false;

	if(IsPosInDeath(Pos))
		return false;

	if(!m_AllowFreeze && IsPosInFreeze(Pos))
		return false;

	int TeleType = 0;
	int TeleNumber = 0;
	const int MapIndex = m_pCollision->GetPureMapIndex(Pos);
	if(m_pCollision->GetTeleTile(MapIndex, TeleType, TeleNumber))
	{
		const bool HasTeleNumber = TeleNumber > 0 && TeleNumber < (int)m_vTeleOuts.size();
		if(TeleType == TILE_TELEIN || TeleType == TILE_TELEINEVIL)
			return HasTeleNumber && !m_vTeleOuts[TeleNumber].empty();
		if(TeleType == TILE_TELEOUT)
			return true;
		if(TeleType == TILE_TELECHECKIN || TeleType == TILE_TELECHECKINEVIL)
			return m_HasAnyTeleCheckOut;
		if(TeleType == TILE_TELECHECK || TeleType == TILE_TELECHECKOUT)
			return true;
		return false;
	}

	return true;
}

bool CDebugPath::IsGroundedPos(const vec2 &Pos) const
{
	if(!m_pCollision)
		return false;

	const float Hs = CCharacterCore::PhysicalSize() / 2.0f;
	return m_pCollision->CheckPoint(Pos.x + Hs, Pos.y + Hs + 5.0f) ||
	       m_pCollision->CheckPoint(Pos.x - Hs, Pos.y + Hs + 5.0f);
}

void CDebugPath::BuildCache()
{
	const size_t Size = (size_t)m_SubWidth * (size_t)m_SubHeight;
	m_vSubPassable.assign(Size, 0);
	m_vSubGrounded.assign(Size, 0);
	m_vSubFinish.assign(Size, 0);
	m_vSubFreeze.assign(Size, 0);
	m_vSubDistance.assign(Size, FIELD_INF);

	for(int sy = 0; sy < m_SubHeight; sy++)
	{
		for(int sx = 0; sx < m_SubWidth; sx++)
		{
			const int Index = SubCellIndex(sx, sy);
			const vec2 Pos = SubCellCenter(sx, sy);

			const bool Freeze = IsPosInFreeze(Pos);
			const bool Safe = IsPosSafe(Pos);
			const bool Grounded = Safe && IsGroundedPos(Pos);
			const bool Finish = IsPosInFinish(Pos);

			m_vSubPassable[Index] = Safe ? 1 : 0;
			m_vSubGrounded[Index] = Grounded ? 1 : 0;
			m_vSubFinish[Index] = Finish ? 1 : 0;
			m_vSubFreeze[Index] = Freeze ? 1 : 0;
		}
	}
}

void CDebugPath::BuildTeleCache()
{
	const size_t TileCount = (size_t)m_Width * (size_t)m_Height;
	m_vTeleType.assign(TileCount, 0);
	m_vTeleNumber.assign(TileCount, 0);
	m_vTeleIns.assign(256, {});
	m_vTeleOuts.assign(256, {});
	m_vTeleCheckOuts.assign(256, {});
	m_vTeleCheckIns.clear();
	m_HasAnyTeleCheckOut = false;

	if(!m_pCollision || TileCount == 0)
		return;

	for(int y = 0; y < m_Height; y++)
	{
		for(int x = 0; x < m_Width; x++)
		{
			const int Index = y * m_Width + x;
			int Type = 0;
			int Number = 0;
			if(!m_pCollision->GetTeleTile(Index, Type, Number))
				continue;

			m_vTeleType[Index] = (unsigned char)std::clamp(Type, 0, 255);
			m_vTeleNumber[Index] = (unsigned char)std::clamp(Number, 0, 255);

			if(Number <= 0 || Number >= (int)m_vTeleIns.size())
				continue;

			const int SubX = x * m_SubDiv + m_SubDiv / 2;
			const int SubY = y * m_SubDiv + m_SubDiv / 2;
			if(SubX < 0 || SubY < 0 || SubX >= m_SubWidth || SubY >= m_SubHeight)
				continue;

			const int SubIndex = SubCellIndex(SubX, SubY);
			if(SubIndex < 0)
				continue;

			if(Type == TILE_TELEIN || Type == TILE_TELEINEVIL)
				m_vTeleIns[Number].push_back(SubIndex);
			else if(Type == TILE_TELEOUT)
				m_vTeleOuts[Number].push_back(SubIndex);
			else if(Type == TILE_TELECHECKOUT)
			{
				m_vTeleCheckOuts[Number].push_back(SubIndex);
				m_HasAnyTeleCheckOut = true;
			}
			else if(Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL)
				m_vTeleCheckIns.push_back(SubIndex);
		}
	}

	for(size_t i = 0; i < m_vTeleIns.size(); i++)
	{
		if(m_vTeleOuts[i].empty())
			m_vTeleIns[i].clear();
	}

	if(m_vTeleCheckIns.empty())
	{
		for(auto &vCheckOuts : m_vTeleCheckOuts)
			vCheckOuts.clear();
	}
}

vec2 CDebugPath::FindNearestSafeSubCell(const vec2 &Pos, int MaxRadiusTiles) const
{
	if(m_SubWidth <= 0 || m_SubHeight <= 0)
		return vec2(-1.0f, -1.0f);

	const float Size = 32.0f / (float)m_SubDiv;
	const int CX = std::clamp((int)(Pos.x / Size), 0, m_SubWidth - 1);
	const int CY = std::clamp((int)(Pos.y / Size), 0, m_SubHeight - 1);

	if(SubCellSafe(CX, CY))
		return SubCellCenter(CX, CY);

	const int MaxRadius = std::max(1, MaxRadiusTiles * m_SubDiv);
	float BestDist = std::numeric_limits<float>::infinity();
	vec2 Best(-1.0f, -1.0f);

	for(int Radius = 1; Radius <= MaxRadius; Radius++)
	{
		for(int DY = -Radius; DY <= Radius; DY++)
		{
			for(int DX = -Radius; DX <= Radius; DX++)
			{
				if(std::abs(DX) != Radius && std::abs(DY) != Radius)
					continue;

				const int NX = CX + DX;
				const int NY = CY + DY;
				if(!SubCellSafe(NX, NY))
					continue;

				const vec2 Candidate = SubCellCenter(NX, NY);
				const float Dist = distance(Candidate, Pos);
				if(Dist < BestDist)
				{
					BestDist = Dist;
					Best = Candidate;
				}
			}
		}
	}

	return Best;
}

void CDebugPath::CollectGoalSubCells(const vec2 &Goal, std::vector<int> &vGoals) const
{
	vGoals.clear();

	if(IsPosInFinish(Goal))
	{
		for(int sy = 0; sy < m_SubHeight; sy++)
		{
			for(int sx = 0; sx < m_SubWidth; sx++)
			{
				if(SubCellSafe(sx, sy) && SubCellFinish(sx, sy))
					vGoals.push_back(SubCellIndex(sx, sy));
			}
		}
		if(!vGoals.empty())
			return;
	}

	const vec2 Base = FindNearestSafeSubCell(Goal, 8);
	if(Base.x < 0.0f || Base.y < 0.0f)
		return;

	const float Size = 32.0f / (float)m_SubDiv;
	const int BX = std::clamp((int)(Base.x / Size), 0, m_SubWidth - 1);
	const int BY = std::clamp((int)(Base.y / Size), 0, m_SubHeight - 1);

	struct CGoalCand
	{
		float m_Score;
		int m_Index;
	};

	std::vector<CGoalCand> vCand;
	vCand.reserve(64);

	for(int DY = -3; DY <= 3; DY++)
	{
		for(int DX = -3; DX <= 3; DX++)
		{
			const int NX = BX + DX;
			const int NY = BY + DY;
			if(!SubCellSafe(NX, NY))
				continue;

			const vec2 P = SubCellCenter(NX, NY);
			float Score = distance(P, Goal);
			if(SubCellGrounded(NX, NY))
				Score -= 1.0f;
			vCand.push_back({Score, SubCellIndex(NX, NY)});
		}
	}

	std::sort(vCand.begin(), vCand.end(), [](const CGoalCand &A, const CGoalCand &B) {
		if(A.m_Score != B.m_Score)
			return A.m_Score < B.m_Score;
		return A.m_Index < B.m_Index;
	});

	for(size_t i = 0; i < vCand.size() && i < 6; i++)
		vGoals.push_back(vCand[i].m_Index);

	std::sort(vGoals.begin(), vGoals.end());
	vGoals.erase(std::unique(vGoals.begin(), vGoals.end()), vGoals.end());
}

int CDebugPath::TeleTypeAtSubIndex(int SubIndex) const
{
	if(SubIndex < 0 || m_vTeleType.empty() || m_SubWidth <= 0)
		return 0;

	const int SubX = SubIndex % m_SubWidth;
	const int SubY = SubIndex / m_SubWidth;
	const int TileX = SubX / m_SubDiv;
	const int TileY = SubY / m_SubDiv;
	if(TileX < 0 || TileY < 0 || TileX >= m_Width || TileY >= m_Height)
		return 0;

	return m_vTeleType[TileY * m_Width + TileX];
}

int CDebugPath::TeleNumberAtSubIndex(int SubIndex) const
{
	if(SubIndex < 0 || m_vTeleNumber.empty() || m_SubWidth <= 0)
		return 0;

	const int SubX = SubIndex % m_SubWidth;
	const int SubY = SubIndex / m_SubWidth;
	const int TileX = SubX / m_SubDiv;
	const int TileY = SubY / m_SubDiv;
	if(TileX < 0 || TileY < 0 || TileX >= m_Width || TileY >= m_Height)
		return 0;

	return m_vTeleNumber[TileY * m_Width + TileX];
}

int CDebugPath::TeleOutNumber(int SubX, int SubY) const
{
	if(m_vTeleType.empty())
		return 0;
	const int TileX = SubX / m_SubDiv;
	const int TileY = SubY / m_SubDiv;
	if(TileX < 0 || TileY < 0 || TileX >= m_Width || TileY >= m_Height)
		return 0;
	const int Index = TileY * m_Width + TileX;
	if(m_vTeleType[Index] == TILE_TELEOUT)
		return m_vTeleNumber[Index];
	return 0;
}

int CDebugPath::TeleCheckOutNumber(int SubX, int SubY) const
{
	if(m_vTeleType.empty())
		return 0;
	const int TileX = SubX / m_SubDiv;
	const int TileY = SubY / m_SubDiv;
	if(TileX < 0 || TileY < 0 || TileX >= m_Width || TileY >= m_Height)
		return 0;
	const int Index = TileY * m_Width + TileX;
	if(m_vTeleType[Index] == TILE_TELECHECKOUT)
		return m_vTeleNumber[Index];
	return 0;
}

void CDebugPath::ApplyCellState(CProgState &State) const
{
	if(State.m_SubIndex < 0)
		return;

	const int Type = TeleTypeAtSubIndex(State.m_SubIndex);
	const int Number = TeleNumberAtSubIndex(State.m_SubIndex);
	if(Type == TILE_TELECHECK && Number > 0)
		State.m_TeleCheckpoint = (unsigned char)std::clamp(Number, 0, 255);
}

void CDebugPath::NormalizeState(const CProgState &State, std::vector<CProgState> &vOutStates, std::vector<std::vector<CEdgeStep>> &vOutEdges) const
{
	vOutStates.clear();
	vOutEdges.clear();

	struct CLocalState
	{
		CProgState m_State;
		std::vector<CEdgeStep> m_vEdge;
	};

	std::queue<CLocalState> Queue;
	Queue.push({State, {}});
	std::unordered_set<CProgState, CStateHash> Visited;

	int Guard = 0;
	const int MaxGuard = 128;

	while(!Queue.empty() && Guard++ < MaxGuard)
	{
		CLocalState Cur = std::move(Queue.front());
		Queue.pop();

		CProgState S = Cur.m_State;
		ApplyCellState(S);

		if(!Visited.insert(S).second)
			continue;

		if(S.m_SubIndex < 0 || S.m_SubIndex >= (int)m_vSubPassable.size())
			continue;
		if(m_vSubPassable[S.m_SubIndex] == 0)
			continue;

		auto PushTeleportDestination = [&](int DstIndex) {
			if(DstIndex < 0 || DstIndex >= (int)m_vSubPassable.size())
				return;
			if(m_vSubPassable[DstIndex] == 0)
				return;

			CLocalState Next;
			Next.m_State = S;
			Next.m_State.m_SubIndex = DstIndex;
			Next.m_vEdge = Cur.m_vEdge;
			Next.m_vEdge.push_back({DstIndex, 1});
			Queue.push(std::move(Next));
		};

		const int Type = TeleTypeAtSubIndex(S.m_SubIndex);
		const int Number = TeleNumberAtSubIndex(S.m_SubIndex);

		if((Type == TILE_TELEIN || Type == TILE_TELEINEVIL) && Number > 0 && Number < (int)m_vTeleOuts.size())
		{
			const auto &vTeleOuts = m_vTeleOuts[Number];
			for(int DstIndex : vTeleOuts)
				PushTeleportDestination(DstIndex);
			continue;
		}

		if((Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL) && S.m_TeleCheckpoint > 0 && S.m_TeleCheckpoint < (int)m_vTeleCheckOuts.size())
		{
			const auto &vTeleCheckOuts = m_vTeleCheckOuts[S.m_TeleCheckpoint];
			for(int DstIndex : vTeleCheckOuts)
				PushTeleportDestination(DstIndex);
			continue;
		}

		if(Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL)
			continue;

		vOutStates.push_back(S);
		vOutEdges.push_back(std::move(Cur.m_vEdge));
	}
}

bool CDebugPath::BuildDistanceField(const vec2 &Goal)
{
	if(!m_Initialized)
		return false;

	std::fill(m_vSubDistance.begin(), m_vSubDistance.end(), FIELD_INF);

	std::vector<int> vGoals;
	CollectGoalSubCells(Goal, vGoals);
	if(vGoals.empty())
		return false;

	using COpenNode = std::pair<int, int>;
	std::priority_queue<COpenNode, std::vector<COpenNode>, std::greater<COpenNode>> Open;

	for(int GoalIndex : vGoals)
	{
		if(GoalIndex < 0 || GoalIndex >= (int)m_vSubDistance.size())
			continue;

		m_vSubDistance[GoalIndex] = 0;
		Open.push({0, GoalIndex});
	}

	const int DirX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
	const int DirY[8] = {0, 0, -1, 1, -1, 1, -1, 1};
	const int MoveCost[8] = {10, 10, 10, 10, 14, 14, 14, 14};
	constexpr int TELE_COST = 1;

	while(!Open.empty())
	{
		const int CurDist = Open.top().first;
		const int CurIndex = Open.top().second;
		Open.pop();

		if(CurIndex < 0 || CurIndex >= (int)m_vSubDistance.size())
			continue;
		if(CurDist != m_vSubDistance[CurIndex])
			continue;

		const int X = CurIndex % m_SubWidth;
		const int Y = CurIndex / m_SubWidth;

		for(int i = 0; i < 8; i++)
		{
			const int NX = X + DirX[i];
			const int NY = Y + DirY[i];
			if(!SubCellSafe(NX, NY))
				continue;

			if(i >= 4)
			{
				if(!SubCellSafe(NX, Y) || !SubCellSafe(X, NY))
					continue;
			}

			int Step = MoveCost[i];
			if(DirY[i] < 0)
				Step += SubCellGrounded(NX, NY) ? 12 : 36;
			else if(DirY[i] > 0)
				Step += SubCellGrounded(NX, NY) ? 2 : 0;
			else if(!SubCellGrounded(NX, NY))
				Step += 4;

			if(SubCellGrounded(X, Y))
				Step = std::max(4, Step - 1);

			const int NIndex = SubCellIndex(NX, NY);
			const int ND = CurDist + Step;
			if(ND >= m_vSubDistance[NIndex])
				continue;

			m_vSubDistance[NIndex] = ND;
			Open.push({ND, NIndex});
		}

		const int TeleOut = TeleOutNumber(X, Y);
		if(TeleOut > 0 && TeleOut < (int)m_vTeleIns.size())
		{
			const auto &vTeleIns = m_vTeleIns[TeleOut];
			for(int TeleInIndex : vTeleIns)
			{
				if(TeleInIndex < 0 || TeleInIndex >= (int)m_vSubDistance.size())
					continue;
				if(m_vSubPassable[TeleInIndex] == 0)
					continue;

				const int ND = CurDist + TELE_COST;
				if(ND >= m_vSubDistance[TeleInIndex])
					continue;

				m_vSubDistance[TeleInIndex] = ND;
				Open.push({ND, TeleInIndex});
			}
		}

		const int TeleCheckOut = TeleCheckOutNumber(X, Y);
		if(TeleCheckOut > 0 && TeleCheckOut < (int)m_vTeleCheckOuts.size() && !m_vTeleCheckIns.empty())
		{
			for(int TeleCheckInIndex : m_vTeleCheckIns)
			{
				if(TeleCheckInIndex < 0 || TeleCheckInIndex >= (int)m_vSubDistance.size())
					continue;
				if(m_vSubPassable[TeleCheckInIndex] == 0)
					continue;

				const int ND = CurDist + TELE_COST;
				if(ND >= m_vSubDistance[TeleCheckInIndex])
					continue;

				m_vSubDistance[TeleCheckInIndex] = ND;
				Open.push({ND, TeleCheckInIndex});
			}
		}
	}

	return true;
}

int CDebugPath::DistanceAtPos(const vec2 &Pos) const
{
	if(!m_Initialized || m_vSubDistance.empty())
		return FIELD_INF;

	const float Size = 32.0f / (float)m_SubDiv;
	const int SX = std::clamp((int)(Pos.x / Size), 0, m_SubWidth - 1);
	const int SY = std::clamp((int)(Pos.y / Size), 0, m_SubHeight - 1);

	int Best = FIELD_INF;

	for(int Radius = 0; Radius <= 2; Radius++)
	{
		for(int DY = -Radius; DY <= Radius; DY++)
		{
			for(int DX = -Radius; DX <= Radius; DX++)
			{
				const int NX = SX + DX;
				const int NY = SY + DY;
				if(!SubCellInBounds(NX, NY))
					continue;

				const int Index = SubCellIndex(NX, NY);
				if(Index < 0 || Index >= (int)m_vSubDistance.size())
					continue;

				Best = std::min(Best, m_vSubDistance[Index]);
			}
		}
	}

	return Best;
}

bool CDebugPath::BuildPath(const vec2 &Start, const vec2 &Goal, std::vector<vec2> &vOutPath)
{
	return BuildPath(Start, Goal, vOutPath, false);
}

bool CDebugPath::BuildPath(const vec2 &Start, const vec2 &Goal, std::vector<vec2> &vOutPath, bool AllowFreeze)
{
	std::vector<unsigned char> vDummyTele;
	return BuildPath(Start, Goal, vOutPath, vDummyTele, AllowFreeze);
}

bool CDebugPath::BuildPath(const vec2 &Start, const vec2 &Goal, std::vector<vec2> &vOutPath, std::vector<unsigned char> &vOutTele, bool AllowFreeze)
{
	vOutPath.clear();
	vOutTele.clear();

	if(!m_Initialized)
		return false;

	if(AllowFreeze != m_AllowFreeze)
	{
		m_AllowFreeze = AllowFreeze;
		BuildCache();
	}

	if(!BuildDistanceField(Goal))
		return false;

	std::vector<int> vGoals;
	CollectGoalSubCells(Goal, vGoals);
	if(vGoals.empty())
		return false;

	std::vector<unsigned char> vGoalMask((size_t)m_SubWidth * (size_t)m_SubHeight, 0);
	for(int GoalIndex : vGoals)
	{
		if(GoalIndex >= 0 && GoalIndex < (int)vGoalMask.size())
			vGoalMask[GoalIndex] = 1;
	}

	const vec2 StartCell = FindNearestSafeSubCell(Start, 8);
	if(StartCell.x < 0.0f || StartCell.y < 0.0f)
		return false;

	const float Size = 32.0f / (float)m_SubDiv;
	const int StartX = std::clamp((int)(StartCell.x / Size), 0, m_SubWidth - 1);
	const int StartY = std::clamp((int)(StartCell.y / Size), 0, m_SubHeight - 1);
	const int StartSubIndex = SubCellIndex(StartX, StartY);

	CProgState RawStartState;
	RawStartState.m_SubIndex = StartSubIndex;
	RawStartState.m_TeleCheckpoint = 0;

	std::vector<CProgState> vStartStates;
	std::vector<std::vector<CEdgeStep>> vStartEdges;
	NormalizeState(RawStartState, vStartStates, vStartEdges);
	if(vStartStates.empty())
		return false;

	const int MaxExpanded = std::max(20000, std::min((int)m_vSubPassable.size() * 8, 300000));

	using COpenEntry = std::pair<int, int>;
	std::priority_queue<COpenEntry, std::vector<COpenEntry>, std::greater<COpenEntry>> Open;
	std::vector<CSearchNode> vNodes;
	vNodes.reserve(std::max(4096, std::min(MaxExpanded, (int)m_vSubPassable.size() * 2)));

	std::unordered_map<CProgState, int, CStateHash> BestG;
	BestG.reserve(8192);

	const auto HeuristicForState = [&](const CProgState &State) {
		if(State.m_SubIndex < 0 || State.m_SubIndex >= (int)m_vSubDistance.size())
			return 0;
		const int D = m_vSubDistance[State.m_SubIndex];
		return D >= FIELD_INF ? 0 : D;
	};

	const auto PushNode = [&](const CProgState &State, int G, int Parent, const std::vector<CEdgeStep> &vEdge, auto &&PushNodeRef) -> void {
		auto It = BestG.find(State);
		if(It != BestG.end() && G >= It->second)
			return;

		BestG[State] = G;

		CSearchNode Node;
		Node.m_State = State;
		Node.m_G = G;
		Node.m_F = G + HeuristicForState(State);
		Node.m_Parent = Parent;
		Node.m_vEdge = vEdge;

		const int NewIndex = (int)vNodes.size();
		vNodes.push_back(std::move(Node));
		Open.push({vNodes[NewIndex].m_F, NewIndex});
	};

	for(size_t i = 0; i < vStartStates.size(); i++)
		PushNode(vStartStates[i], 0, -1, vStartEdges[i], PushNode);

	const int DirX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
	const int DirY[8] = {0, 0, -1, 1, -1, 1, -1, 1};
	const int BaseCost[8] = {10, 10, 10, 10, 14, 14, 14, 14};

	int Expanded = 0;
	int GoalNode = -1;

	while(!Open.empty() && Expanded < MaxExpanded)
	{
		const int NodeIndex = Open.top().second;
		Open.pop();

		if(NodeIndex < 0 || NodeIndex >= (int)vNodes.size())
			continue;

		const CSearchNode CurNode = vNodes[NodeIndex];

		const auto It = BestG.find(CurNode.m_State);
		if(It == BestG.end() || CurNode.m_G != It->second)
			continue;

		if(CurNode.m_State.m_SubIndex >= 0 && CurNode.m_State.m_SubIndex < (int)vGoalMask.size() && vGoalMask[CurNode.m_State.m_SubIndex] != 0)
		{
			GoalNode = NodeIndex;
			break;
		}

		Expanded++;

		const int X = CurNode.m_State.m_SubIndex % m_SubWidth;
		const int Y = CurNode.m_State.m_SubIndex / m_SubWidth;

		for(int i = 0; i < 8; i++)
		{
			const int NX = X + DirX[i];
			const int NY = Y + DirY[i];
			if(!SubCellSafe(NX, NY))
				continue;

			if(i >= 4)
			{
				if(!SubCellSafe(NX, Y) || !SubCellSafe(X, NY))
					continue;
			}

			const int NIndex = SubCellIndex(NX, NY);
			CProgState StepState = CurNode.m_State;
			StepState.m_SubIndex = NIndex;

			std::vector<CProgState> vNormStates;
			std::vector<std::vector<CEdgeStep>> vNormEdges;
			NormalizeState(StepState, vNormStates, vNormEdges);
			if(vNormStates.empty())
				continue;

			int StepCost = BaseCost[i];
			if(DirY[i] < 0)
				StepCost += SubCellGrounded(NX, NY) ? 12 : 36;
			else if(DirY[i] > 0)
				StepCost += SubCellGrounded(NX, NY) ? 2 : 0;
			else if(!SubCellGrounded(NX, NY))
				StepCost += 4;

			if(SubCellGrounded(X, Y))
				StepCost = std::max(4, StepCost - 1);

			if(AllowFreeze && NIndex >= 0 && NIndex < (int)m_vSubFreeze.size() && m_vSubFreeze[NIndex] != 0)
				StepCost += 300;
			if(AllowFreeze && CurNode.m_State.m_SubIndex >= 0 && CurNode.m_State.m_SubIndex < (int)m_vSubFreeze.size() && m_vSubFreeze[CurNode.m_State.m_SubIndex] != 0)
				StepCost += 60;

			for(size_t k = 0; k < vNormStates.size(); k++)
			{
				std::vector<CEdgeStep> vEdge;
				vEdge.reserve(1 + vNormEdges[k].size());
				vEdge.push_back({NIndex, 0});
				for(const CEdgeStep &Step : vNormEdges[k])
					vEdge.push_back(Step);

				int ExtraCost = 0;
				for(const CEdgeStep &Step : vNormEdges[k])
				{
					if(Step.m_Tele)
						ExtraCost += 1;
				}

				PushNode(vNormStates[k], CurNode.m_G + StepCost + ExtraCost, NodeIndex, vEdge, PushNode);
			}
		}
	}

	if(GoalNode < 0)
		return false;

	std::vector<int> vChain;
	for(int Node = GoalNode; Node >= 0; Node = vNodes[Node].m_Parent)
		vChain.push_back(Node);
	std::reverse(vChain.begin(), vChain.end());

	auto AddPoint = [&](const vec2 &P, bool Tele) {
		if(vOutPath.empty())
		{
			vOutPath.push_back(P);
			return;
		}
		if(distance(vOutPath.back(), P) <= 4.0f)
			return;
		vOutPath.push_back(P);
		vOutTele.push_back(Tele ? 1 : 0);
	};

	AddPoint(Start, false);

	const vec2 StartCellPos = SubCellCenter(StartX, StartY);
	if(distance(vOutPath.back(), StartCellPos) > 4.0f)
		AddPoint(StartCellPos, false);

	for(int NodeIndex : vChain)
	{
		const CSearchNode &Node = vNodes[NodeIndex];
		for(const CEdgeStep &Step : Node.m_vEdge)
		{
			if(Step.m_SubIndex < 0 || Step.m_SubIndex >= (int)m_vSubPassable.size())
				continue;
			const int SX = Step.m_SubIndex % m_SubWidth;
			const int SY = Step.m_SubIndex / m_SubWidth;
			AddPoint(SubCellCenter(SX, SY), Step.m_Tele != 0);
		}
	}

	if(vOutPath.empty())
		return false;

	if(distance(vOutPath.back(), Goal) > 8.0f)
		AddPoint(Goal, false);

	return vOutPath.size() >= 2;
}

bool CDebugPath::SamplePathMetrics(const std::vector<vec2> &vPath, const vec2 &Pos,
	float &OutProgress, float &OutDistance, vec2 &OutDir) const
{
	static const std::vector<unsigned char> s_EmptyTele;
	return SamplePathMetrics(vPath, s_EmptyTele, Pos, OutProgress, OutDistance, OutDir);
}

bool CDebugPath::SamplePathMetrics(const std::vector<vec2> &vPath, const std::vector<unsigned char> &vTele, const vec2 &Pos,
	float &OutProgress, float &OutDistance, vec2 &OutDir) const
{
	OutProgress = 0.0f;
	OutDistance = 0.0f;
	OutDir = vec2(0.0f, 0.0f);

	if(vPath.size() < 2)
		return false;

	const bool UseTele = vTele.size() + 1 == vPath.size();
	float BestDistSq = std::numeric_limits<float>::infinity();
	float BestProgress = 0.0f;
	vec2 BestDir(0.0f, 0.0f);

	float AccumLen = 0.0f;

	for(size_t i = 1; i < vPath.size(); i++)
	{
		if(UseTele && vTele[i - 1])
			continue;

		const vec2 A = vPath[i - 1];
		const vec2 B = vPath[i];
		const vec2 AB = B - A;
		const float LenSq = dot(AB, AB);
		if(LenSq <= 0.0001f)
			continue;

		const float Len = std::sqrt(LenSq);
		const float T = std::clamp(dot(Pos - A, AB) / LenSq, 0.0f, 1.0f);
		const vec2 Closest = A + AB * T;
		const vec2 Delta = Pos - Closest;
		const float DistSq = dot(Delta, Delta);

		if(DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestProgress = AccumLen + Len * T;
			BestDir = AB / Len;
		}

		AccumLen += Len;
	}

	if(!std::isfinite(BestDistSq))
		return false;

	OutProgress = BestProgress;
	OutDistance = std::sqrt(BestDistSq);
	OutDir = BestDir;
	return true;
}

float CDebugPath::ScorePathFollow(const std::vector<vec2> &vPath, const vec2 &From, const vec2 &To) const
{
	static const std::vector<unsigned char> s_EmptyTele;
	return ScorePathFollow(vPath, s_EmptyTele, From, To);
}

float CDebugPath::ScorePathFollow(const std::vector<vec2> &vPath, const std::vector<unsigned char> &vTele, const vec2 &From, const vec2 &To) const
{
	float FromProgress = 0.0f;
	float FromDistance = 0.0f;
	vec2 FromDir(0.0f, 0.0f);

	float ToProgress = 0.0f;
	float ToDistance = 0.0f;
	vec2 ToDir(0.0f, 0.0f);

	if(!SamplePathMetrics(vPath, vTele, From, FromProgress, FromDistance, FromDir))
		return 0.0f;
	if(!SamplePathMetrics(vPath, vTele, To, ToProgress, ToDistance, ToDir))
		return 0.0f;

	float Score = 0.0f;
	Score += (ToProgress - FromProgress) * 0.08f;
	Score += (FromDistance - ToDistance) * 0.06f;
	Score -= std::max(0.0f, ToDistance - 24.0f) * 0.03f;
	return Score;
}
