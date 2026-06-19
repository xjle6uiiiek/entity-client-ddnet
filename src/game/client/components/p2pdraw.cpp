#include <base/color.h>
#include <base/system.h>
#include <engine/engine.h>
#include <engine/console.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include "p2pdraw.h"

CP2PDraw::CP2PDraw()
{
	m_Socket.type = NETTYPE_INVALID;
	m_IsHost = false;
	m_IsActive = false;
	m_IsDrawing = false;
}

CP2PDraw::~CP2PDraw()
{
	Disconnect();
}

void CP2PDraw::OnInit()
{
}

void CP2PDraw::OnConsoleInit()
{
	Console()->Register("draw_host", "i", CFGFLAG_CLIENT, ConHost, this, "Host P2P drawing session on specified port");
	Console()->Register("draw_connect", "si", CFGFLAG_CLIENT, ConConnect, this, "Connect to P2P drawing session (IP Port)");
}

void CP2PDraw::Host(int Port)
{
	Disconnect();
	
	NETADDR BindAddr;
	mem_zero(&BindAddr, sizeof(BindAddr));
	BindAddr.type = NETTYPE_IPV4;
	BindAddr.port = Port;

	m_Socket = net_udp_create(BindAddr);
	if(m_Socket.type != NETTYPE_INVALID)
	{
		m_IsHost = true;
		m_IsActive = true;
		m_vClients.clear();
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "p2pdraw", "Successfully hosted P2P drawing session.");
	}
	else
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "p2pdraw", "Failed to host P2P drawing session.");
	}
}

void CP2PDraw::Connect(const char *pIP, int Port)
{
	Disconnect();

	NETADDR HostAddr;
	if(net_host_lookup(pIP, &HostAddr, m_pClient->Engine()->SocketPool()->GetPoolType()) != 0)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "p2pdraw", "Failed to resolve host IP.");
		return;
	}
	HostAddr.port = Port;

	NETADDR BindAddr;
	mem_zero(&BindAddr, sizeof(BindAddr));
	BindAddr.type = HostAddr.type;
	BindAddr.port = 0; // Random port

	m_Socket = net_udp_create(BindAddr);
	if(m_Socket.type != NETTYPE_INVALID)
	{
		m_IsHost = false;
		m_IsActive = true;
		m_vClients.clear();
		m_vClients.push_back(HostAddr);

		// Send connect packet
		CPacket P;
		mem_zero(&P, sizeof(P));
		P.m_Type = PACKET_CONNECT;
		net_udp_send(m_Socket, &HostAddr, &P, sizeof(P));

		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "p2pdraw", "Connected to P2P drawing session.");
	}
	else
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "p2pdraw", "Failed to create socket for P2P drawing.");
	}
}

void CP2PDraw::Disconnect()
{
	if(m_Socket.type != NETTYPE_INVALID)
	{
		net_udp_close(m_Socket);
		m_Socket.type = NETTYPE_INVALID;
	}
	m_IsActive = false;
	m_vClients.clear();
}

void CP2PDraw::SendToClients(const void *pData, int Size, const NETADDR *pIgnore)
{
	if(!m_IsActive) return;

	for(const auto& Addr : m_vClients)
	{
		if(pIgnore && net_addr_comp(&Addr, pIgnore) == 0)
			continue;
		net_udp_send(m_Socket, &Addr, pData, Size);
	}
}

void CP2PDraw::AddPoint(vec2 Pos, bool NewStroke, ColorRGBA Color, float Width, float LifetimeSec)
{
	if(NewStroke)
	{
		if(!m_CurrentStroke.m_aPoints.empty())
		{
			m_vStrokes.push_back(m_CurrentStroke);
			m_CurrentStroke.m_aPoints.clear();
		}
		
		m_CurrentStroke.m_Color = Color;
		m_CurrentStroke.m_Width = Width;
		m_CurrentStroke.m_CreationTime = time_get();
		m_CurrentStroke.m_LifetimeFreq = (int64_t)(LifetimeSec * time_freq());
		m_CurrentStroke.m_FadeTimeFreq = (int64_t)(1.5f * time_freq()); // 1.5 seconds fade animation
		m_CurrentStroke.m_aPoints.push_back({Pos});
	}
	else
	{
		if(!m_CurrentStroke.m_aPoints.empty())
		{
			// Only add if it moved enough
			if(distance(m_CurrentStroke.m_aPoints.back().m_Pos, Pos) > 2.0f)
			{
				m_CurrentStroke.m_aPoints.push_back({Pos});
			}
		}
	}
}

void CP2PDraw::ProcessPackets()
{
	if(!m_IsActive) return;

	NETADDR Addr;
	unsigned char *pData;
	int Bytes;

	while((Bytes = net_udp_recv(m_Socket, &Addr, &pData)) > 0)
	{
		if(Bytes == sizeof(CPacket))
		{
			CPacket *pPkt = (CPacket*)pData;

			// Host accepts new clients automatically when receiving a packet
			if(m_IsHost)
			{
				bool Found = false;
				for(const auto& C : m_vClients)
				{
					if(net_addr_comp(&C, &Addr) == 0)
					{
						Found = true;
						break;
					}
				}
				if(!Found)
				{
					m_vClients.push_back(Addr);
				}
			}

			if(pPkt->m_Type == PACKET_NEW_STROKE || pPkt->m_Type == PACKET_LINE_TO)
			{
				ColorRGBA Col = ColorRGBA((pPkt->m_Color >> 24 & 0xFF) / 255.0f,
										  (pPkt->m_Color >> 16 & 0xFF) / 255.0f,
										  (pPkt->m_Color >> 8 & 0xFF) / 255.0f,
										  (pPkt->m_Color & 0xFF) / 255.0f);

				AddPoint(vec2(pPkt->m_X, pPkt->m_Y), pPkt->m_Type == PACKET_NEW_STROKE, Col, pPkt->m_Width, pPkt->m_Lifetime);
				
				if(m_IsHost) // Relay to other clients
				{
					SendToClients(pPkt, sizeof(CPacket), &Addr);
				}
			}
		}
	}
}

void CP2PDraw::OnRender()
{
	ProcessPackets();

	// Local Drawing Logic
	if(m_IsActive && Input()->KeyPressed(KEY_MOUSE_2)) // Right click to draw for testing, or we can use a bind
	{
		vec2 MousePos = m_pClient->m_Controls.m_MousePos; // Real world coordinates
		
		bool IsNew = !m_IsDrawing;
		m_IsDrawing = true;
		
		ColorRGBA MyColor = ColorRGBA(1.0f, 0.2f, 0.2f, 1.0f);
		float MyWidth = 10.0f;
		float MyLifetime = 10.0f;

		AddPoint(MousePos, IsNew, MyColor, MyWidth, MyLifetime);

		CPacket P;
		mem_zero(&P, sizeof(P));
		P.m_Type = IsNew ? PACKET_NEW_STROKE : PACKET_LINE_TO;
		P.m_X = MousePos.x;
		P.m_Y = MousePos.y;
		P.m_Color = ((int)(MyColor.r * 255) << 24) | ((int)(MyColor.g * 255) << 16) | ((int)(MyColor.b * 255) << 8) | (int)(MyColor.a * 255);
		P.m_Width = MyWidth;
		P.m_Lifetime = MyLifetime;

		SendToClients(&P, sizeof(P));
	}
	else if(m_IsDrawing)
	{
		m_IsDrawing = false;
		if(!m_CurrentStroke.m_aPoints.empty())
		{
			m_vStrokes.push_back(m_CurrentStroke);
			m_CurrentStroke.m_aPoints.clear();
		}
	}

	// Render strokes
	int64_t Now = time_get();
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	
	auto RenderStroke = [&](CDrawingStroke& Stroke) {
		if(Stroke.m_aPoints.size() < 2) return;

		int64_t AliveTime = Now - Stroke.m_CreationTime;
		if(AliveTime > Stroke.m_LifetimeFreq + Stroke.m_FadeTimeFreq)
			return; // Dead

		float Alpha = 1.0f;
		float Progress = 0.0f;

		if(AliveTime > Stroke.m_LifetimeFreq)
		{
			Progress = (float)(AliveTime - Stroke.m_LifetimeFreq) / (float)Stroke.m_FadeTimeFreq;
			// Smooth fade out from the end (first points drawn disappear first)
		}

		size_t StartIdx = (size_t)(Progress * Stroke.m_aPoints.size());
		if(StartIdx >= Stroke.m_aPoints.size() - 1) return;

		Graphics()->SetColor(Stroke.m_Color.r, Stroke.m_Color.g, Stroke.m_Color.b, Stroke.m_Color.a);
		// Note: DDNet's lines don't natively support width in IGraphics::LinesBegin, we just draw lines.
		// Alternatively, we could draw quads. For simplicity, we use lines.

		for(size_t i = StartIdx; i < Stroke.m_aPoints.size() - 1; ++i)
		{
			IGraphics::CLineItem Line(Stroke.m_aPoints[i].m_Pos.x, Stroke.m_aPoints[i].m_Pos.y, Stroke.m_aPoints[i+1].m_Pos.x, Stroke.m_aPoints[i+1].m_Pos.y);
			Graphics()->LinesDraw(&Line, 1);
		}
	};

	for(auto& Stroke : m_vStrokes)
		RenderStroke(Stroke);
		
	if(!m_CurrentStroke.m_aPoints.empty())
		RenderStroke(m_CurrentStroke);

	Graphics()->LinesEnd();

	// Clean up dead strokes
	for(size_t i = 0; i < m_vStrokes.size(); )
	{
		if(Now > m_vStrokes[i].m_CreationTime + m_vStrokes[i].m_LifetimeFreq + m_vStrokes[i].m_FadeTimeFreq)
			m_vStrokes.erase(m_vStrokes.begin() + i);
		else
			++i;
	}
}

void CP2PDraw::ConHost(IConsole::IResult *pResult, void *pUserData)
{
	CP2PDraw *pSelf = (CP2PDraw *)pUserData;
	pSelf->Host(pResult->GetInteger(0));
}

void CP2PDraw::ConConnect(IConsole::IResult *pResult, void *pUserData)
{
	CP2PDraw *pSelf = (CP2PDraw *)pUserData;
	pSelf->Connect(pResult->GetString(0), pResult->GetInteger(1));
}
