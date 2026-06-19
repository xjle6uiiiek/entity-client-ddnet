#ifndef GAME_CLIENT_COMPONENTS_P2PDRAW_H
#define GAME_CLIENT_COMPONENTS_P2PDRAW_H

#include <base/vmath.h>
#include <base/net.h>
#include <game/client/component.h>
#include <vector>

struct CStrokePoint {
	vec2 m_Pos;
};

struct CDrawingStroke {
	std::vector<CStrokePoint> m_aPoints;
	ColorRGBA m_Color;
	float m_Width;
	int64_t m_CreationTime;
	int64_t m_LifetimeFreq;
	int64_t m_FadeTimeFreq;
};

class CP2PDraw : public CComponent
{
	NETSOCKET m_Socket;
	bool m_IsHost;
	bool m_IsActive;
	std::vector<NETADDR> m_vClients; // For host: connected clients. For client: m_vClients[0] is host.

	std::vector<CDrawingStroke> m_vStrokes;
	CDrawingStroke m_CurrentStroke;
	bool m_IsDrawing;
	vec2 m_LastMousePos;

	void ProcessPackets();
	void SendToClients(const void *pData, int Size, const NETADDR *pIgnore = nullptr);
	void AddPoint(vec2 Pos, bool NewStroke, ColorRGBA Color, float Width, float LifetimeSec);
	
	enum
	{
		PACKET_NEW_STROKE = 1,
		PACKET_LINE_TO = 2,
		PACKET_CONNECT = 3
	};

	struct CPacket
	{
		int m_Type;
		float m_X;
		float m_Y;
		int m_Color;
		float m_Width;
		float m_Lifetime;
	};

public:
	CP2PDraw();
	virtual ~CP2PDraw();

	virtual int Sizeof() const override { return sizeof(*this); }
	virtual void OnRender() override;
	virtual void OnConsoleInit() override;
	virtual void OnInit() override;

	void Host(int Port);
	void Connect(const char *pIP, int Port);
	void Disconnect();

	static void ConHost(IConsole::IResult *pResult, void *pUserData);
	static void ConConnect(IConsole::IResult *pResult, void *pUserData);
};

#endif
