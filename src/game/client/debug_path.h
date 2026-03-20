#ifndef GAME_CLIENT_DEBUG_PATH_H
#define GAME_CLIENT_DEBUG_PATH_H

#include <base/vmath.h>

#include <vector>

class CCollision;

class CDebugPath
{
	static constexpr int FIELD_INF = 1 << 29;

	CCollision *m_pCollision;
	int m_Width;
	int m_Height;
	int m_SubDiv;
	int m_SubWidth;
	int m_SubHeight;
	bool m_Initialized;
	bool m_AllowFreeze;
	bool m_HasAnyTeleCheckOut;

	std::vector<unsigned char> m_vSubPassable;
	std::vector<unsigned char> m_vSubGrounded;
	std::vector<unsigned char> m_vSubFinish;
	std::vector<int> m_vSubDistance;
	std::vector<unsigned char> m_vTeleType;
	std::vector<unsigned char> m_vTeleNumber;
	std::vector<std::vector<int>> m_vTeleIns;
	std::vector<std::vector<int>> m_vTeleOuts;
	std::vector<int> m_vTeleCheckIns;
	std::vector<std::vector<int>> m_vTeleCheckOuts;

	bool SubCellInBounds(int X, int Y) const;
	int SubCellIndex(int X, int Y) const;
	vec2 SubCellCenter(int X, int Y) const;
	bool SubCellSafe(int X, int Y) const;
	bool SubCellGrounded(int X, int Y) const;
	bool SubCellFinish(int X, int Y) const;

	void CollectTouchedIndices(const vec2 &Pos, int (&aIndices)[5]) const;
	bool MapIndexDeath(int Index) const;
	bool MapIndexFreeze(int Index) const;
	bool MapIndexFinish(int Index) const;

	bool IsPosInDeath(const vec2 &Pos) const;
	bool IsPosInFreeze(const vec2 &Pos) const;
	bool IsPosInFinish(const vec2 &Pos) const;
	bool IsPosSafe(const vec2 &Pos) const;
	bool IsGroundedPos(const vec2 &Pos) const;

	void BuildCache();
	void BuildTeleCache();
	void CollectGoalSubCells(const vec2 &Goal, std::vector<int> &vGoals) const;
	vec2 FindNearestSafeSubCell(const vec2 &Pos, int MaxRadiusTiles) const;
	bool BuildDistanceField(const vec2 &Goal);
	int TeleInNumber(int SubX, int SubY) const;
	int TeleOutNumber(int SubX, int SubY) const;
	bool TeleCheckInAt(int SubX, int SubY) const;
	int TeleCheckOutNumber(int SubX, int SubY) const;

public:
	CDebugPath();

	void Init(CCollision *pCollision, int SubDiv = 5);
	bool IsInitialized() const;

	bool BuildPath(const vec2 &Start, const vec2 &Goal, std::vector<vec2> &vOutPath, std::vector<unsigned char> &vOutTele, bool AllowFreeze);
	int DistanceAtPos(const vec2 &Pos) const;

	bool SamplePathMetrics(const std::vector<vec2> &vPath, const std::vector<unsigned char> &vTele, const vec2 &Pos,
		float &OutProgress, float &OutDistance, vec2 &OutDir) const;

	float ScorePathFollow(const std::vector<vec2> &vPath, const vec2 &From, const vec2 &To) const;
};

#endif
