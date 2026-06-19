#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_BG_DRAW_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_BG_DRAW_H

#include <engine/client/enums.h>
#include <engine/console.h>
#include <base/net.h>

#include <game/client/component.h>

#include <array>
#include <list>
#include <optional>
#include <vector>
#include <map>

#define BG_DRAW_MAX_POINTS_PER_ITEM 1024

class CBgDrawItem;

struct CNetBgDrawPacket {
	int m_Type; // 0=NEW, 1=POINT, 2=END, 3=ERASE, 4=RESET, 5=CONNECT
	int m_StrokeId;
	float x, y, w, r, g, b, a;
	float m_Lifetime;
};

class CBgDraw : public CComponent
{
private:
	float m_NextAutoSave;
	bool m_Dirty;
	std::array<std::optional<CBgDrawItem *>, NUM_DUMMIES> m_apActiveItems;
	std::array<std::optional<vec2>, NUM_DUMMIES> m_aLastPos;
	std::list<CBgDrawItem> *m_pvItems;
	
	// P2P Networking
	NETSOCKET m_Socket;
	bool m_IsHost;
	bool m_IsNetActive;
	std::vector<NETADDR> m_vClients;
	std::map<int, CBgDrawItem*> m_NetActiveItems;
	int m_LocalStrokeId;

	static void ConBgDraw(IConsole::IResult *pResult, void *pUserData);
	static void ConBgDrawErase(IConsole::IResult *pResult, void *pUserData);
	static void ConBgDrawReset(IConsole::IResult *pResult, void *pUserData);
	static void ConBgDrawSave(IConsole::IResult *pResult, void *pUserData);
	static void ConBgDrawLoad(IConsole::IResult *pResult, void *pUserData);
	
	static void ConBgDrawHost(IConsole::IResult *pResult, void *pUserData);
	static void ConBgDrawConnect(IConsole::IResult *pResult, void *pUserData);

	void Reset();
	bool Save(const char *pFile, bool Verbose);
	bool Load(const char *pFile, bool Verbose);
	template<typename... T>
	CBgDrawItem *AddItem(T &&...Args);
	void MakeSpaceFor(int Count);

	void ProcessPackets();
	void SendToClients(const void *pData, int Size, const NETADDR *pIgnore = nullptr);

public:
	enum class InputMode
	{
		NONE = 0,
		DRAW = 1,
		ERASE = 2,
	};
	std::array<InputMode, NUM_DUMMIES> m_aInputData;

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMapLoad() override;
	void OnShutdown() override;

	CBgDraw();
	~CBgDraw() override;
};

#endif
