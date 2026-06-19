#include "bg_draw.h"

#include <base/io.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/external/spt.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/components/tclient/bg_draw_file.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <deque>
#include <vector>

#define MAX_ITEMS_TO_LOAD 65536

constexpr float AUTO_SAVE_INTERVAL = 60.0f;

static float cross(vec2 A, vec2 B)
{
	return A.x * B.y - A.y * B.x;
}

static bool line_intersects(vec2 A, vec2 B, vec2 C, vec2 D)
{
	const vec2 R = B - A;
	const vec2 S = D - C;
	const vec2 Ac = C - A;
	const float Denom = cross(R, S);
	const float Num1 = cross(Ac, S);
	const float Num2 = cross(Ac, R);
	if(Denom == 0.0f)
		return false;
	float T = Num1 / Denom;
	float U = Num2 / Denom;
	return (T >= 0.0f && T <= 1.0f) && (U >= 0.0f && U <= 1.0f);
}

class CBoundingBox
{
public:
	vec2 m_Min = vec2(0.0f, 0.0f);
	vec2 m_Max = vec2(0.0f, 0.0f);
	void ExtendBoundingBox(vec2 Pos, float Radius)
	{
		const vec2 MinPos = Pos - vec2(Radius, Radius);
		const vec2 MaxPos = Pos + vec2(Radius, Radius);
		if(m_Min == m_Max)
		{
			m_Min = MinPos;
			m_Max = MaxPos;
			return;
		}
		if(MinPos.x < m_Min.x)
			m_Min.x = MinPos.x;
		if(MinPos.y < m_Min.y)
			m_Min.y = MinPos.y;
		if(MaxPos.x > m_Max.x)
			m_Max.x = MaxPos.x;
		if(MaxPos.y > m_Max.y)
			m_Max.y = MaxPos.y;
	}
};

class CPathContainer
{
private:
	static constexpr int MAX_QUADS_PER_CONTAINER = 512; // Don't crash on OGL
	std::vector<int> m_vQuadContainerIndexes;
	int m_QuadCount = 0;
	IGraphics &m_Graphics;
	void Clear()
	{
		for(int QuadContainerIndex : m_vQuadContainerIndexes)
			m_Graphics.DeleteQuadContainer(QuadContainerIndex);
		m_vQuadContainerIndexes.clear();
		m_QuadCount = 0;
	}
	void Upload()
	{
		int &QuadContainerIndex = *(m_vQuadContainerIndexes.end() - 1);
		m_Graphics.QuadContainerUpload(QuadContainerIndex);
	}
	void AddQuads(IGraphics::CFreeformItem *pFreeformItem, int Count, ColorRGBA Color)
	{
		m_QuadCount += Count;
		if(m_vQuadContainerIndexes.empty())
		{
			m_vQuadContainerIndexes.push_back(m_Graphics.CreateQuadContainer(false));
		}
		else if(m_QuadCount > MAX_QUADS_PER_CONTAINER)
		{
			int &QuadContainerIndex = *(m_vQuadContainerIndexes.end() - 1);
			m_Graphics.QuadContainerUpload(QuadContainerIndex);
			m_vQuadContainerIndexes.push_back(m_Graphics.CreateQuadContainer(false));
			m_QuadCount = 0;
		}
		int &QuadContainerIndex = *(m_vQuadContainerIndexes.end() - 1);
		m_Graphics.SetColor(Color);
		m_Graphics.QuadContainerAddQuads(QuadContainerIndex, pFreeformItem, Count);
	}
	void AddCircle(vec2 Pos, float Angle1, float Angle2, float Width, ColorRGBA Color)
	{
		const float Radius = Width / 2.0f;
		const int Segments = (int)(std::fabs(Angle2 - Angle1) / (pi / 8.0f));
		const float SegmentAngle = (Angle2 - Angle1) / Segments;
		IGraphics::CFreeformItem FreeformItems[20];
		for(int i = 0; i < Segments; ++i)
		{
			const float A1 = Angle1 + (float)i * SegmentAngle;
			const float A2 = A1 + SegmentAngle;
			const vec2 P1 = Pos + direction(A1) * Radius;
			const vec2 P2 = Pos + direction(A2) * Radius;
			FreeformItems[i] = IGraphics::CFreeformItem(P1.x, P1.y, P2.x, P2.y, Pos.x, Pos.y, Pos.x, Pos.y);
		}
		AddQuads(FreeformItems, Segments, Color);
	}

public:
	CPathContainer(IGraphics &Graphics) :
		m_Graphics(Graphics) {}
	void Update(const CBgDrawItemData &Data)
	{
		Clear();
		if(Data.empty())
		{
			return;
		}
		size_t i = 0;
		auto SPTGetPt = [&](SPT_pt *pPt) {
			if(i >= Data.size())
				return false;
			pPt->color = {Data[i].r, Data[i].g, Data[i].b, Data[i].a};
			pPt->pos = {Data[i].x, Data[i].y};
			pPt->w = Data[i].w;
			i += 1;
			return true;
		};
		auto SPTAddQuad = [&](SPT_quad Quad, SPT_color Color) {
			IGraphics::CFreeformItem FreeformItem(
				Quad.a.x, Quad.a.y,
				Quad.b.x, Quad.b.y,
				Quad.d.x, Quad.d.y,
				Quad.c.x, Quad.c.y);
			AddQuads(&FreeformItem, 1, {Color.r, Color.g, Color.b, Color.a});
			return true;
		};
		auto SPTAddArc = [&](SPT_vec2 Pos, float A1, float A2, float R, SPT_color Color) {
			AddCircle({Pos.x, Pos.y}, A1, A2, R * 2.0f, {Color.r, Color.g, Color.b, Color.a});
			return true;
		};
		SPT_prims(SPTGetPt, SPTAddQuad, SPTAddArc);
		Upload();
	}
	void Render()
	{
		for(int QuadContainerIndex : m_vQuadContainerIndexes)
			m_Graphics.RenderQuadContainer(QuadContainerIndex, -1);
	}
	~CPathContainer()
	{
		Clear();
	}
};

class CBgDrawItem
{
private:
	CGameClient &m_This;

	int m_Drawing = true;

	CBoundingBox m_BoundingBox;
	CPathContainer m_PathContainer;
	CBgDrawItemData m_Data;

	float CurrentWidth() const
	{
		return (float)g_Config.m_TcBgDrawWidth * m_This.m_Camera.m_Zoom;
	}
	ColorRGBA CurrentColor() const
	{
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcBgDrawColor));
	}

public:
	bool m_Killed = false;
	float m_SecondsAge = 0.0f;
	float m_Lifetime = 0.0f;

	const CBgDrawItemData &Data() const { return m_Data; }
	const CBoundingBox &BoundingBox() const { return m_BoundingBox; }

	bool PenUp(const CBgDrawItemDataPoint &Point)
	{
		if(!m_Drawing)
			return false;
		m_Drawing = false;
		m_PathContainer.Update(m_Data);
		return true;
	}
	bool PenUp(vec2 Pos)
	{
		return PenUp({Pos, CurrentWidth(), CurrentColor()});
	}
	bool MoveTo(const CBgDrawItemDataPoint &Point)
	{
		if(!m_Drawing)
			return false;
		// Length limit removed
		if(m_Data.size() >= 2 && distance((m_Data.end() - 2)->Pos(), (m_Data.end() - 1)->Pos()) < Point.w * 0.75f)
			*(m_Data.end() - 1) = Point;
		else
			m_Data.emplace_back(Point);
		m_PathContainer.Update(m_Data);
		return true;
	}
	bool MoveTo(vec2 Pos)
	{
		return MoveTo(CBgDrawItemDataPoint(Pos, CurrentWidth(), CurrentColor()));
	}

	void PopFrontPoints(size_t Num)
	{
		if(Num == 0 || m_Data.empty()) return;
		if(Num >= m_Data.size()) {
			m_Data.clear();
		} else {
			m_Data.erase(m_Data.begin(), m_Data.begin() + Num);
		}
		m_PathContainer.Update(m_Data);
	}

	size_t OriginalSize = 0; // To keep track of original length for smooth fading

	bool PointIntersect(const vec2 Pos, const float Radius) const
	{
		if(m_Data.empty())
			return true;
		if(m_Data.size() == 1)
			return distance(m_Data[0].Pos(), Pos) < m_Data[0].w + Radius;
		vec2 C, D = m_Data[0].Pos();
		for(auto It = std::next(m_Data.begin()); It != m_Data.end(); ++It)
		{
			const CBgDrawItemDataPoint &Point = *It;
			C = D;
			D = Point.Pos();
			vec2 Closest;
			if(!closest_point_on_line(C, D, Pos, Closest))
				continue;
			if(distance(Closest, Pos) < Radius + Point.w)
				return true;
		}
		return false;
	}
	bool LineIntersect(const vec2 A, const vec2 B) const
	{
		if(m_Data.empty())
			return true;
		if(m_Data.size() == 1)
		{
			vec2 P = m_Data[0].Pos();
			vec2 Closest;
			if(!closest_point_on_line(A, B, P, Closest))
				return false;
			return distance(Closest, P) < m_Data[0].w;
		}
		vec2 C, D = m_Data[0].Pos();
		for(auto It = std::next(m_Data.begin()); It != m_Data.end(); ++It)
		{
			const CBgDrawItemDataPoint &Point = *It;
			C = D;
			D = Point.Pos();
			if(line_intersects(A, B, C, D))
				return true;
		}
		return false;
	}
	void Render()
	{
		m_PathContainer.Render();
	}

	CBgDrawItem() = delete;
	CBgDrawItem(CGameClient &This, vec2 StartPos) :
		m_This(This), m_PathContainer(*This.Graphics())
	{
		m_BoundingBox.ExtendBoundingBox(StartPos, CurrentWidth());
		m_Data.emplace_back(StartPos, CurrentWidth(), CurrentColor());
		m_Lifetime = (float)g_Config.m_TcBgDrawFadeTime;
	}
	CBgDrawItem(CGameClient &This, CBgDrawItemDataPoint StartPoint) :
		CBgDrawItem(This, StartPoint.Pos())
	{
		m_Data.clear();
		m_Data.push_back(StartPoint);
		m_Lifetime = (float)g_Config.m_TcBgDrawFadeTime;
	}
	CBgDrawItem(CGameClient &This, const CBgDrawItemData &Data) :
		CBgDrawItem(This, Data[0])
	{
		if(Data.size() > 1)
			for(auto It = Data.begin() + 1; It != Data.end() - 1; ++It)
			{
				const CBgDrawItemDataPoint &Point = *It;
				MoveTo(Point);
			}
		PenUp(Data.back());
	}
};

static void DoInput(CBgDraw &This, CBgDraw::InputMode Mode, int Value)
{
	CBgDraw::InputMode &Input = This.m_aInputData[g_Config.m_ClDummy];
	if(Value > 0)
		Input = Mode;
	else if(Input == Mode)
		Input = CBgDraw::InputMode::NONE;
}

void CBgDraw::ConBgDraw(IConsole::IResult *pResult, void *pUserData)
{
	DoInput(*(CBgDraw *)pUserData, InputMode::DRAW, pResult->GetInteger(0));
}

void CBgDraw::ConBgDrawErase(IConsole::IResult *pResult, void *pUserData)
{
	DoInput(*(CBgDraw *)pUserData, InputMode::ERASE, pResult->GetInteger(0));
}

void CBgDraw::ConBgDrawReset(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pThis = (CBgDraw *)pUserData;
	pThis->Reset();
	if(pThis->m_IsNetActive)
	{
		CNetBgDrawPacket Pkt;
		Pkt.m_Type = 4; // RESET
		pThis->SendToClients(&Pkt, sizeof(Pkt));
	}
}

void CBgDraw::ConBgDrawSave(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pThis = (CBgDraw *)pUserData;
	pThis->Save(pResult->GetString(0), true);
}

void CBgDraw::ConBgDrawLoad(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pThis = (CBgDraw *)pUserData;
	pThis->Load(pResult->GetString(0), true);
}

static IOHANDLE BgDrawOpenFile(CGameClient &This, const char *pFilename, int Flags)
{
	char aFilename[IO_MAX_PATH_LENGTH];
	if(pFilename && pFilename[0] != '\0')
	{
		if(str_endswith_nocase(pFilename, ".csv"))
			str_format(aFilename, sizeof(aFilename), "bgdraw/%s", pFilename);
		else
			str_format(aFilename, sizeof(aFilename), "bgdraw/%s.csv", pFilename);
	}
	else
	{
		SHA256_DIGEST Sha256 = This.Map()->Sha256();
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(Sha256, aSha256, sizeof(aSha256));
		str_format(aFilename, sizeof(aFilename), "bgdraw/%s_%s.csv", This.Map()->BaseName(), aSha256);
	}
	dbg_assert(Flags == IOFLAG_WRITE || Flags == IOFLAG_READ, "Flags must be either read or write");
	if(Flags == IOFLAG_WRITE)
	{
		// Create folder
		if(!This.Storage()->CreateFolder("bgdraw", IStorage::TYPE_SAVE))
			This.Echo(("Failed to create bgdraw folder"));
	}
	return This.Storage()->OpenFile(aFilename, Flags, IStorage::TYPE_SAVE);
}

bool CBgDraw::Save(const char *pFilename, bool Verbose)
{
	if(m_pvItems->empty())
	{
		if(Verbose)
			GameClient()->Echo(("No items to write"));
		return false;
	}
	if(!m_Dirty)
	{
		if(Verbose)
			GameClient()->Echo(("No changes since last save"));
		return false;
	}
	m_Dirty = false;
	IOHANDLE Handle = BgDrawOpenFile(*GameClient(), pFilename, IOFLAG_WRITE);
	if(!Handle)
		return false;
	int Written = 0;
	bool Success = true;
	char aMsg[256];
	for(const CBgDrawItem &Item : *m_pvItems)
	{
		if(!BgDrawFile::Write(Handle, Item.Data()))
		{
			str_format(aMsg, sizeof(aMsg), ("Writing item %d failed"), Written);
			GameClient()->Echo(aMsg);
			Success = false;
			break;
		}
		Written += 1;
	}
	if(Verbose || !Success)
	{
		str_format(aMsg, sizeof(aMsg), ("Written %d items"), Written);
		GameClient()->Echo(aMsg);
	}
	io_close(Handle);
	return Success;
}

bool CBgDraw::Load(const char *pFilename, bool Verbose)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	IOHANDLE Handle = BgDrawOpenFile(*GameClient(), pFilename, IOFLAG_READ);
	if(!Handle)
		return false;
	std::deque<CBgDrawItemData> Queue;
	int ItemsLoaded = 0;
	int ItemsDiscarded = 0;
	{
		CBgDrawItemData Data;
		while(BgDrawFile::Read(Handle, Data) && (ItemsLoaded++) < MAX_ITEMS_TO_LOAD)
		{
			if((int)Queue.size() > g_Config.m_TcBgDrawMaxItems)
			{
				ItemsDiscarded += 1;
				Queue.pop_front();
			}
			Queue.push_back(Data);
		}
	}
	io_close(Handle);
	MakeSpaceFor(Queue.size());
	for(const CBgDrawItemData &Data : Queue)
		AddItem(*GameClient(), Data);
	if(Verbose)
	{
		char aInfo[256];
		if(ItemsDiscarded == 0)
			str_format(aInfo, sizeof(aInfo), "Loaded %d items", ItemsLoaded);
		else
			str_format(aInfo, sizeof(aInfo), "Loaded %d items (discarded %d items)", ItemsLoaded - ItemsDiscarded, ItemsDiscarded);
		GameClient()->Echo(aInfo);
	}

	return true;
}

template<typename... T>
CBgDrawItem *CBgDraw::AddItem(T &&...Aargs)
{
	MakeSpaceFor(1);
	if(g_Config.m_TcBgDrawMaxItems == 0)
		return nullptr;
	m_pvItems->emplace_back(std::forward<T>(Aargs)...);
	m_Dirty = true;
	return &m_pvItems->back();
}

void CBgDraw::MakeSpaceFor(int Count)
{
	if(g_Config.m_TcBgDrawMaxItems == 0 || Count >= g_Config.m_TcBgDrawMaxItems)
	{
		m_pvItems->clear();
		return;
	}
	while((int)m_pvItems->size() + Count > g_Config.m_TcBgDrawMaxItems)
	{
		// Prevent floating pointer
		for(std::optional<CBgDrawItem *> &ActiveItem : m_apActiveItems)
		{
			if(!ActiveItem.has_value())
				continue;
			if(ActiveItem.value() != &(*m_pvItems->begin()))
				continue;
			ActiveItem = std::nullopt;
		}
		m_pvItems->pop_front();
	}
}

void CBgDraw::OnConsoleInit()
{
	Console()->Register("+bg_draw", "", CFGFLAG_CLIENT, ConBgDraw, this, "Draw on the in game background");
	Console()->Register("+bg_draw_erase", "", CFGFLAG_CLIENT, ConBgDrawErase, this, "Erase items on the in game background");
	Console()->Register("bg_draw_reset", "", CFGFLAG_CLIENT, ConBgDrawReset, this, "Reset all drawings on the background");
	Console()->Register("bg_draw_save", "?r[filename]", CFGFLAG_CLIENT, ConBgDrawSave, this, "Save drawings to a given csv file (defaults to map name)");
	Console()->Register("bg_draw_load", "?r[filename]", CFGFLAG_CLIENT, ConBgDrawLoad, this, "Load drawings from a given csv file (defaults to map name)");
	Console()->Register("draw_host", "i", CFGFLAG_CLIENT, ConBgDrawHost, this, "Host P2P drawing session on specified port");
	Console()->Register("draw_connect", "?s?i", CFGFLAG_CLIENT, ConBgDrawConnect, this, "Connect to P2P drawing session (IP Port)");
}

void CBgDraw::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	ProcessPackets();
	Graphics()->TextureClear();

	float Delta = Client()->RenderFrameTime();

	m_NextAutoSave -= Delta;
	if(m_NextAutoSave < 0)
	{
		if(g_Config.m_TcBgDrawAutoSaveLoad)
			Save(nullptr, false);
		m_NextAutoSave = AUTO_SAVE_INTERVAL;
	}

	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		// Handle updating active item
		const InputMode Input = m_aInputData[Dummy];
		std::optional<CBgDrawItem *> &ActiveItem = m_apActiveItems[Dummy];
		vec2 Pos;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active && Dummy == g_Config.m_ClDummy)
			Pos = GameClient()->m_Camera.m_Center;
		else
			Pos = (GameClient()->m_Controls.m_aTargetPos[Dummy] - GameClient()->m_Camera.m_Center) * GameClient()->m_Camera.m_Zoom + GameClient()->m_Camera.m_Center;
		if(Input == InputMode::DRAW)
		{
			if(ActiveItem.has_value())
			{
				(*ActiveItem)->MoveTo(Pos);
				if(m_IsNetActive)
				{
					CNetBgDrawPacket Pkt;
					Pkt.m_Type = 1; // POINT
					Pkt.m_StrokeId = m_LocalStrokeId + Dummy;
					Pkt.x = Pos.x; Pkt.y = Pos.y;
					Pkt.w = (float)g_Config.m_TcBgDrawWidth * GameClient()->m_Camera.m_Zoom;
					ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcBgDrawColor));
					Pkt.r = Col.r; Pkt.g = Col.g; Pkt.b = Col.b; Pkt.a = Col.a; Pkt.m_Lifetime = (float)g_Config.m_TcBgDrawFadeTime;
					SendToClients(&Pkt, sizeof(Pkt));
				}
			}
			else
			{
				ActiveItem = AddItem(*GameClient(), Pos);
				if(m_IsNetActive)
				{
					m_LocalStrokeId = rand(); // new random stroke id
					CNetBgDrawPacket Pkt;
					Pkt.m_Type = 0; // NEW
					Pkt.m_StrokeId = m_LocalStrokeId + Dummy;
					Pkt.x = Pos.x; Pkt.y = Pos.y;
					Pkt.w = (float)g_Config.m_TcBgDrawWidth * GameClient()->m_Camera.m_Zoom;
					ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcBgDrawColor));
					Pkt.r = Col.r; Pkt.g = Col.g; Pkt.b = Col.b; Pkt.a = Col.a; Pkt.m_Lifetime = (float)g_Config.m_TcBgDrawFadeTime;
					SendToClients(&Pkt, sizeof(Pkt));
				}
			}
			m_Dirty = true;
		}
		else if(ActiveItem.has_value())
		{
			(*ActiveItem)->PenUp(Pos);
			if(m_IsNetActive)
			{
				CNetBgDrawPacket Pkt;
				Pkt.m_Type = 2; // END
				Pkt.m_StrokeId = m_LocalStrokeId + Dummy;
				Pkt.x = Pos.x; Pkt.y = Pos.y;
				Pkt.w = (float)g_Config.m_TcBgDrawWidth * GameClient()->m_Camera.m_Zoom;
				ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcBgDrawColor));
				Pkt.r = Col.r; Pkt.g = Col.g; Pkt.b = Col.b; Pkt.a = Col.a; Pkt.m_Lifetime = (float)g_Config.m_TcBgDrawFadeTime;
				SendToClients(&Pkt, sizeof(Pkt));
			}
			ActiveItem = std::nullopt;
		}
		std::optional<vec2> &LastPos = m_aLastPos[Dummy];
		if(Input == InputMode::ERASE)
		{
			if(LastPos.has_value())
			{
				for(CBgDrawItem &Item : *m_pvItems)
					if(Item.LineIntersect(*LastPos, Pos))
						Item.m_Killed = true;
			}
			else
			{
				for(CBgDrawItem &Item : *m_pvItems)
					if(Item.PointIntersect(Pos, 2.0f))
						Item.m_Killed = true;
			}
			
			if(m_IsNetActive)
			{
				CNetBgDrawPacket Pkt;
				Pkt.m_Type = 3; // ERASE
				Pkt.m_StrokeId = 0;
				Pkt.x = Pos.x; Pkt.y = Pos.y;
				Pkt.w = 2.0f * GameClient()->m_Camera.m_Zoom; // Approx erase width
				Pkt.r = 0; Pkt.g = 0; Pkt.b = 0; Pkt.a = 0;
				SendToClients(&Pkt, sizeof(Pkt));
			}
		}
		else
		{
			LastPos = std::nullopt;
		}
	}
	// Remove extra items
	MakeSpaceFor(0);
	// Update age of items, delete old items, render items
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	for(CBgDrawItem &Item : *m_pvItems)
	{
		// If this item is currently active
		if(std::any_of(std::begin(m_apActiveItems), std::end(m_apActiveItems), [&](const std::optional<CBgDrawItem *> &ActiveItem) {
			   return ActiveItem.value_or(nullptr) == &Item;
		   }))
		{
			Item.m_SecondsAge = 0.0f;
		}
		else
		{
			Item.m_SecondsAge += Delta;
			if(Item.m_Lifetime > 0.0f)
			{
				if (Item.OriginalSize == 0) Item.OriginalSize = Item.Data().size();
				float FadeTime = Item.m_Lifetime;
				float FadeOutStart = FadeTime * 0.5f; // Start fading in second half of lifetime
				if(Item.m_SecondsAge > FadeTime)
					Item.m_Killed = true;
				else if(Item.m_SecondsAge > FadeOutStart)
				{
					float Progress = (Item.m_SecondsAge - FadeOutStart) / (FadeTime - FadeOutStart);
					size_t TargetSize = (size_t)((1.0f - Progress) * Item.OriginalSize);
					if(TargetSize < Item.Data().size())
						Item.PopFrontPoints(Item.Data().size() - TargetSize);
				}
			}
		}
		const bool InRangeX = Item.BoundingBox().m_Min.x < ScreenX1 || Item.BoundingBox().m_Max.x > ScreenX0;
		const bool InRangeY = Item.BoundingBox().m_Min.y < ScreenY1 || Item.BoundingBox().m_Max.y > ScreenY0;
		if(InRangeX && InRangeY)
			Item.Render();
	}
	// Remove killed items
	if(m_pvItems->remove_if([&](CBgDrawItem &Item) { return Item.m_Killed; }))
		m_Dirty = true;
	Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

void CBgDraw::Reset()
{
	m_aInputData.fill(InputMode::NONE);
	m_aLastPos.fill(std::nullopt);
	m_apActiveItems.fill(std::nullopt);
	m_pvItems->clear();
}

void CBgDraw::OnMapLoad()
{
}

void CBgDraw::OnStateChange(int NewState, int OldState)
{
	if(OldState == IClient::STATE_ONLINE || OldState == IClient::STATE_DEMOPLAYBACK)
	{
		if(g_Config.m_TcBgDrawAutoSaveLoad > 0)
			Save(nullptr, false);
	}
	Reset();
	m_NextAutoSave = AUTO_SAVE_INTERVAL;
	if(NewState == IClient::STATE_ONLINE || OldState == IClient::STATE_DEMOPLAYBACK)
	{
		if(g_Config.m_TcBgDrawAutoSaveLoad > 0)
			Load(nullptr, false);
	}
}

void CBgDraw::OnShutdown()
{
	Reset();
}

CBgDraw::CBgDraw() :
	m_pvItems(new std::list<CBgDrawItem>())
{
	m_Socket = nullptr;
	m_IsHost = false;
	m_IsNetActive = false;
	m_LocalStrokeId = 0;
}

CBgDraw::~CBgDraw()
{
	if(m_Socket != nullptr) { net_udp_close(m_Socket); m_Socket = nullptr; }
	delete m_pvItems;
}

void CBgDraw::ConBgDrawHost(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pSelf = (CBgDraw *)pUserData;
	if(pSelf->m_Socket != nullptr && net_socket_type(pSelf->m_Socket) != NETTYPE_INVALID)
		net_udp_close(pSelf->m_Socket);

	NETADDR BindAddr;
	mem_zero(&BindAddr, sizeof(BindAddr));
	BindAddr.type = NETTYPE_IPV4;
	BindAddr.port = pResult->GetInteger(0);

	pSelf->m_Socket = net_udp_create(BindAddr);
	if(pSelf->m_Socket != nullptr && net_socket_type(pSelf->m_Socket) != NETTYPE_INVALID)
	{
		pSelf->m_IsHost = true;
		pSelf->m_IsNetActive = true;
		pSelf->m_vClients.clear();
		pSelf->m_NetActiveItems.clear();
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "bg_draw", "Successfully hosted P2P drawing session.");
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "bg_draw", "Failed to host P2P drawing session.");
	}
}

void CBgDraw::ConBgDrawConnect(IConsole::IResult *pResult, void *pUserData)
{
	CBgDraw *pSelf = (CBgDraw *)pUserData;
	if(pSelf->m_Socket != nullptr && net_socket_type(pSelf->m_Socket) != NETTYPE_INVALID)
		net_udp_close(pSelf->m_Socket);

	NETADDR HostAddr;
	if(net_host_lookup(pResult->GetString(0), &HostAddr, NETTYPE_IPV4) != 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "bg_draw", "Failed to resolve host IP.");
		return;
	}
	HostAddr.port = pResult->GetInteger(1);

	NETADDR BindAddr;
	mem_zero(&BindAddr, sizeof(BindAddr));
	BindAddr.type = HostAddr.type;
	BindAddr.port = 0;

	pSelf->m_Socket = net_udp_create(BindAddr);
	if(pSelf->m_Socket != nullptr && net_socket_type(pSelf->m_Socket) != NETTYPE_INVALID)
	{
		pSelf->m_IsHost = false;
		pSelf->m_IsNetActive = true;
		pSelf->m_vClients.clear();
		pSelf->m_vClients.push_back(HostAddr);
		pSelf->m_NetActiveItems.clear();

		CNetBgDrawPacket P;
		mem_zero(&P, sizeof(P));
		P.m_Type = 5; // CONNECT
		net_udp_send(pSelf->m_Socket, &HostAddr, &P, sizeof(P));

		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "bg_draw", "Connected to P2P drawing session.");
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "bg_draw", "Failed to create socket for P2P drawing.");
	}
}

void CBgDraw::SendToClients(const void *pData, int Size, const NETADDR *pIgnore)
{
	if(!m_IsNetActive) return;

	for(const auto& Addr : m_vClients)
	{
		if(pIgnore && net_addr_comp(&Addr, pIgnore) == 0)
			continue;
		net_udp_send(m_Socket, &Addr, pData, Size);
	}
}

void CBgDraw::ProcessPackets()
{
	if(!m_IsNetActive || m_Socket == nullptr || net_socket_type(m_Socket) == NETTYPE_INVALID) return;

	NETADDR Addr;
	unsigned char *pData;
	int Bytes;

	while((Bytes = net_udp_recv(m_Socket, &Addr, &pData)) > 0)
	{
		if(Bytes == sizeof(CNetBgDrawPacket))
		{
			CNetBgDrawPacket *pPkt = (CNetBgDrawPacket*)pData;

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
				
				// Relay if it's drawing data
				if(pPkt->m_Type != 5) // CONNECT
					SendToClients(pPkt, sizeof(CNetBgDrawPacket), &Addr);
			}

			if(pPkt->m_Type == 0) // NEW
			{
				CBgDrawItemDataPoint Pt(pPkt->x, pPkt->y, pPkt->w, pPkt->r, pPkt->g, pPkt->b, pPkt->a);
				CBgDrawItem* pItem = AddItem(*GameClient(), Pt);
				if(pItem) pItem->m_Lifetime = pPkt->m_Lifetime;
				m_NetActiveItems[pPkt->m_StrokeId] = pItem;
			}
			else if(pPkt->m_Type == 1) // POINT
			{
				if(m_NetActiveItems.count(pPkt->m_StrokeId))
				{
					CBgDrawItemDataPoint Pt(pPkt->x, pPkt->y, pPkt->w, pPkt->r, pPkt->g, pPkt->b, pPkt->a);
					m_NetActiveItems[pPkt->m_StrokeId]->MoveTo(Pt);
				}
			}
			else if(pPkt->m_Type == 2) // END
			{
				if(m_NetActiveItems.count(pPkt->m_StrokeId))
				{
					CBgDrawItemDataPoint Pt(pPkt->x, pPkt->y, pPkt->w, pPkt->r, pPkt->g, pPkt->b, pPkt->a);
					m_NetActiveItems[pPkt->m_StrokeId]->PenUp(Pt);
					m_NetActiveItems.erase(pPkt->m_StrokeId);
				}
			}
			else if(pPkt->m_Type == 3) // ERASE
			{
				for(CBgDrawItem &Item : *m_pvItems)
					if(Item.PointIntersect(vec2(pPkt->x, pPkt->y), pPkt->w))
						Item.m_Killed = true;
			}
			else if(pPkt->m_Type == 4) // RESET
			{
				Reset();
			}
		}
	}
}
