#ifndef GAME_CLIENT_COMPONENTS_ENTITY_MAP_PROGRESS_H
#define GAME_CLIENT_COMPONENTS_ENTITY_MAP_PROGRESS_H

#include <base/vmath.h>

#include <game/client/component.h>
#include <game/client/debug_path.h>

#include <vector>

class CMapProgress : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMapLoad() override;
	void OnReset() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

private:
	void ResetState();
	void CollectTileCandidates();
	bool BuildPathIfNeeded(const vec2 &Pos);
	bool GetTargetPos(vec2 &OutPos) const;
	static float PathLength(const std::vector<vec2> &vPath);
	static float PathLength(const std::vector<vec2> &vPath, const std::vector<unsigned char> &vTele);
	static vec2 FindNearest(const std::vector<vec2> &vCandidates, const vec2 &Pos);

	CDebugPath m_Path;
	std::vector<vec2> m_vPath;
	std::vector<unsigned char> m_vPathTele;
	std::vector<vec2> m_vStartCandidates;
	std::vector<vec2> m_vFinishCandidates;
	vec2 m_StartPos = vec2(0.0f, 0.0f);
	vec2 m_GoalPos = vec2(0.0f, 0.0f);
	bool m_HasStart = false;
	bool m_HasGoal = false;
	bool m_PathValid = false;
	bool m_PathAttempted = false;
	float m_TotalLength = 0.0f;
};

#endif
