#include "debug_path.h"

#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <game/gamecore.h>

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
	m_vSubDistance.assign(Size, FIELD_INF);

	for(int sy = 0; sy < m_SubHeight; sy++)
	{
		for(int sx = 0; sx < m_SubWidth; sx++)
		{
			const int Index = SubCellIndex(sx, sy);
			const vec2 Pos = SubCellCenter(sx, sy);

			const bool Safe = IsPosSafe(Pos);
			const bool Grounded = Safe && IsGroundedPos(Pos);
			const bool Finish = IsPosInFinish(Pos);

			m_vSubPassable[Index] = Safe ? 1 : 0;
			m_vSubGrounded[Index] = Grounded ? 1 : 0;
			m_vSubFinish[Index] = Finish ? 1 : 0;
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

			if(Number <= 0 || Number >= (int)m_vTeleIns.size())
				continue;

			const bool IsTeleIn = Type == TILE_TELEIN || Type == TILE_TELEINEVIL;
			const bool IsTeleOut = Type == TILE_TELEOUT;
			const bool IsTeleCheckOut = Type == TILE_TELECHECKOUT;
			const bool IsTeleCheckIn = Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL;

			if(!IsTeleIn && !IsTeleOut && !IsTeleCheckOut && !IsTeleCheckIn)
				continue;

			m_vTeleType[Index] = (unsigned char)Type;
			m_vTeleNumber[Index] = (unsigned char)Number;

			const int SubX = x * m_SubDiv + m_SubDiv / 2;
			const int SubY = y * m_SubDiv + m_SubDiv / 2;
			if(SubX < 0 || SubY < 0 || SubX >= m_SubWidth || SubY >= m_SubHeight)
				continue;

			const int SubIndex = SubCellIndex(SubX, SubY);
			if(SubIndex < 0)
				continue;

			if(IsTeleIn)
				m_vTeleIns[Number].push_back(SubIndex);
			else if(IsTeleOut)
				m_vTeleOuts[Number].push_back(SubIndex);
			else if(IsTeleCheckOut)
			{
				m_vTeleCheckOuts[Number].push_back(SubIndex);
				m_HasAnyTeleCheckOut = true;
			}
			else if(IsTeleCheckIn)
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

int CDebugPath::TeleInNumber(int SubX, int SubY) const
{
	if(m_vTeleType.empty())
		return 0;
	const int TileX = SubX / m_SubDiv;
	const int TileY = SubY / m_SubDiv;
	if(TileX < 0 || TileY < 0 || TileX >= m_Width || TileY >= m_Height)
		return 0;
	const int Index = TileY * m_Width + TileX;
	const int Type = m_vTeleType[Index];
	if(Type == TILE_TELEIN || Type == TILE_TELEINEVIL)
		return m_vTeleNumber[Index];
	return 0;
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

bool CDebugPath::TeleCheckInAt(int SubX, int SubY) const
{
	if(m_vTeleType.empty())
		return false;
	const int TileX = SubX / m_SubDiv;
	const int TileY = SubY / m_SubDiv;
	if(TileX < 0 || TileY < 0 || TileX >= m_Width || TileY >= m_Height)
		return false;
	const int Index = TileY * m_Width + TileX;
	const int Type = m_vTeleType[Index];
	return Type == TILE_TELECHECKIN || Type == TILE_TELECHECKINEVIL;
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

bool CDebugPath::BuildPath(const vec2 &Start, const vec2 &Goal, std::vector<vec2> &vOutPath, std::vector<unsigned char> &vOutTele, bool AllowFreeze)
{
	vOutPath.clear();
	vOutTele.clear();

	if(AllowFreeze != m_AllowFreeze)
	{
		m_AllowFreeze = AllowFreeze;
		BuildCache();
	}

	if(!BuildDistanceField(Goal))
		return false;

	const vec2 StartCell = FindNearestSafeSubCell(Start, 8);
	if(StartCell.x < 0.0f || StartCell.y < 0.0f)
		return false;

	const float Size = 32.0f / (float)m_SubDiv;
	int X = std::clamp((int)(StartCell.x / Size), 0, m_SubWidth - 1);
	int Y = std::clamp((int)(StartCell.y / Size), 0, m_SubHeight - 1);

	auto AddPoint = [&](const vec2 &P, bool Tele) {
		if(vOutPath.empty())
		{
			vOutPath.push_back(P);
			return;
		}
		vOutPath.push_back(P);
		vOutTele.push_back(Tele ? 1 : 0);
	};

	AddPoint(Start, false);

	const int DirX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
	const int DirY[8] = {0, 0, -1, 1, -1, 1, -1, 1};

	const int MaxSteps = std::max(1, m_SubWidth * m_SubHeight);

	bool ReachedGoal = false;
	for(int Step = 0; Step < MaxSteps; Step++)
	{
		if(!SubCellInBounds(X, Y))
			break;

		const int Index = SubCellIndex(X, Y);
		if(Index < 0 || Index >= (int)m_vSubDistance.size())
			break;

		const int CurDist = m_vSubDistance[Index];
		if(CurDist >= FIELD_INF)
		{
			vOutPath.clear();
			vOutTele.clear();
			return false;
		}

		if(CurDist == 0)
		{
			const vec2 GoalPos = SubCellCenter(X, Y);
			if(distance(vOutPath.back(), GoalPos) > 4.0f)
				AddPoint(GoalPos, false);
			ReachedGoal = true;
			break;
		}

		const int TeleIn = TeleInNumber(X, Y);
		if(TeleIn > 0 && TeleIn < (int)m_vTeleOuts.size() && !m_vTeleOuts[TeleIn].empty())
		{
			int TeleBestIndex = -1;
			int TeleBestDist = FIELD_INF;
			const auto &vTeleOuts = m_vTeleOuts[TeleIn];
			for(int TeleOutIndex : vTeleOuts)
			{
				if(TeleOutIndex < 0 || TeleOutIndex >= (int)m_vSubDistance.size())
					continue;
				const int ND = m_vSubDistance[TeleOutIndex];
				if(ND < TeleBestDist)
				{
					TeleBestDist = ND;
					TeleBestIndex = TeleOutIndex;
				}
			}

			if(TeleBestIndex == -1 || TeleBestDist >= CurDist)
			{
				vOutPath.clear();
				vOutTele.clear();
				return false;
			}

			const vec2 TeleInPos = SubCellCenter(X, Y);
			if(distance(vOutPath.back(), TeleInPos) > 4.0f)
				AddPoint(TeleInPos, false);

			const int TeleOutX = TeleBestIndex % m_SubWidth;
			const int TeleOutY = TeleBestIndex / m_SubWidth;
			const vec2 TeleOutPos = SubCellCenter(TeleOutX, TeleOutY);
			AddPoint(TeleOutPos, true);

			X = TeleOutX;
			Y = TeleOutY;
			continue;
		}

		if(TeleCheckInAt(X, Y) && !m_vTeleCheckIns.empty())
		{
			int TeleBestIndex = -1;
			int TeleBestDist = FIELD_INF;
			for(const auto &vCheckOuts : m_vTeleCheckOuts)
			{
				for(int TeleOutIndex : vCheckOuts)
				{
					if(TeleOutIndex < 0 || TeleOutIndex >= (int)m_vSubDistance.size())
						continue;
					const int ND = m_vSubDistance[TeleOutIndex];
					if(ND < TeleBestDist)
					{
						TeleBestDist = ND;
						TeleBestIndex = TeleOutIndex;
					}
				}
			}

			if(TeleBestIndex == -1 || TeleBestDist >= CurDist)
			{
				vOutPath.clear();
				vOutTele.clear();
				return false;
			}

			const vec2 TeleInPos = SubCellCenter(X, Y);
			if(distance(vOutPath.back(), TeleInPos) > 4.0f)
				AddPoint(TeleInPos, false);

			const int TeleOutX = TeleBestIndex % m_SubWidth;
			const int TeleOutY = TeleBestIndex / m_SubWidth;
			const vec2 TeleOutPos = SubCellCenter(TeleOutX, TeleOutY);
			AddPoint(TeleOutPos, true);

			X = TeleOutX;
			Y = TeleOutY;
			continue;
		}

		int BestX = X;
		int BestY = Y;
		int BestDist = CurDist;

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
			if(NIndex < 0 || NIndex >= (int)m_vSubDistance.size())
				continue;

			const int ND = m_vSubDistance[NIndex];
			if(ND < BestDist)
			{
				BestDist = ND;
				BestX = NX;
				BestY = NY;
			}
		}

		if(BestX == X && BestY == Y)
		{
			vOutPath.clear();
			vOutTele.clear();
			return false;
		}

		X = BestX;
		Y = BestY;

		const vec2 P = SubCellCenter(X, Y);
		if(distance(vOutPath.back(), P) > 4.0f)
			AddPoint(P, false);
	}

	if(!ReachedGoal)
	{
		vOutPath.clear();
		vOutTele.clear();
		return false;
	}

	if(!vOutPath.empty() && distance(vOutPath.back(), Goal) > 8.0f)
		AddPoint(Goal, false);

	return vOutPath.size() >= 2;
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
	float FromProgress = 0.0f;
	float FromDistance = 0.0f;
	vec2 FromDir(0.0f, 0.0f);

	float ToProgress = 0.0f;
	float ToDistance = 0.0f;
	vec2 ToDir(0.0f, 0.0f);

	if(!SamplePathMetrics(vPath, s_EmptyTele, From, FromProgress, FromDistance, FromDir))
		return 0.0f;
	if(!SamplePathMetrics(vPath, s_EmptyTele, To, ToProgress, ToDistance, ToDir))
		return 0.0f;

	float Score = 0.0f;
	Score += (ToProgress - FromProgress) * 0.08f;
	Score += (FromDistance - ToDistance) * 0.06f;
	Score -= std::max(0.0f, ToDistance - 24.0f) * 0.03f;
	return Score;
}
