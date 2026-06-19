#include "editor_history.h"

#include "editor.h"
#include "editor_actions.h"

#include <engine/client.h>
#include <game/client/gameclient.h>
#include <engine/shared/config.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol_ex.h>

void CEditorHistory::RecordAction(const std::shared_ptr<IEditorAction> &pAction)
{
	RecordAction(pAction, nullptr, false);
}

void CEditorHistory::Execute(const std::shared_ptr<IEditorAction> &pAction, const char *pDisplay, bool Remote)
{
	pAction->Redo();

	// Local verification of action serialization
	if(pAction->GetType() == 1) // Brush Draw Action
	{
		CPacker Packer;
		pAction->Serialize(&Packer);

		CUnpacker Unpacker;
		Unpacker.Reset(Packer.Data(), Packer.Size());

		auto pDeserialized = CEditorBrushDrawAction::Deserialize(Map(), &Unpacker);
		if(pDeserialized)
		{
			dbg_msg("editor_test", "Round-trip serialization successful for CEditorBrushDrawAction!");
		}
		else
		{
			dbg_msg("editor_test", "ERROR: Round-trip serialization FAILED for CEditorBrushDrawAction!");
		}
	}

	RecordAction(pAction, pDisplay, Remote);
}

void CEditorHistory::RecordAction(const std::shared_ptr<IEditorAction> &pAction, const char *pDisplay, bool Remote)
{
	if(!Remote)
	{
		dbg_msg("collab_debug", "Local RecordAction: State=%d", Editor()->Client()->State());
	}

	// Check locks for local actions
	if(!Remote && Editor()->Client()->State() == IClient::STATE_ONLINE)
	{
		int ActionType = pAction->GetType();
		if(ActionType == 1) // Brush Draw Action
		{
			CPacker Packer;
			Packer.AddInt(ActionType);
			pAction->Serialize(&Packer);

			CUnpacker Unpacker;
			Unpacker.Reset(Packer.Data(), Packer.Size());

			// Skip ActionType
			Unpacker.GetInt(); 
			int GroupIndex = Unpacker.GetInt();
			int LayerCount = Unpacker.GetInt();
			if(!Unpacker.Error())
			{
				for(int i = 0; i < LayerCount; i++)
				{
					int LayerIndex = Unpacker.GetInt();
					if(Unpacker.Error())
						break;

					// Check if this layer is locked by someone else
					std::pair<int, int> Key(GroupIndex, LayerIndex);
					auto it = Editor()->m_RemoteLocks.find(Key);
					CGameClient *pGameClient = (CGameClient *)Editor()->Kernel()->RequestInterface<IGameClient>();
					int LocalClientId = pGameClient ? pGameClient->m_Snap.m_LocalClientId : -1;
					dbg_msg("collab_debug", "RecordAction lock check: locked=%d owner=%d local=%d", 
						(it != Editor()->m_RemoteLocks.end()), 
						(it != Editor()->m_RemoteLocks.end() ? it->second : -2),
						LocalClientId);

					if(it != Editor()->m_RemoteLocks.end() && pGameClient && it->second != LocalClientId)
					{
						dbg_msg("editor", "Rejected local action: Layer (Group=%d, Layer=%d) is locked by ClientId=%d",
							GroupIndex, LayerIndex, it->second);
						
						// Revert action locally
						pAction->Undo();
						str_copy(Editor()->m_aTooltip, "Layer locked by another creator!");
						return; // Reject completely
					}

					// Skip tile changes for this layer
					int ChangesSize = Unpacker.GetInt();
					if(Unpacker.Error())
						break;
					for(int c = 0; c < ChangesSize; c++)
					{
						Unpacker.GetInt(); // y
						int LineSize = Unpacker.GetInt();
						if(Unpacker.Error())
							break;
						for(int l = 0; l < LineSize; l++)
						{
							Unpacker.GetInt(); // x
							Unpacker.GetRaw(sizeof(CTile)); // m_Previous
							Unpacker.GetRaw(sizeof(CTile)); // m_Current
							if(Unpacker.Error())
								break;
						}
						if(Unpacker.Error())
							break;
					}
					if(Unpacker.Error())
						break;
				}
			}
		}
	}

	// Send message to server if it's a local action and we are online
	if(!Remote && Editor()->Client()->State() == IClient::STATE_ONLINE)
	{
		dbg_msg("collab_debug", "Sending NETMSG_EDITOR_ACTION to server");
		CMsgPacker Msg(NETMSG_EDITOR_ACTION, false);
		Msg.AddInt(pAction->GetType());
		pAction->Serialize(&Msg);
		Editor()->Client()->SendMsgActive(&Msg, MSGFLAG_VITAL);
	}

	if(m_IsBulk)
	{
		m_vpBulkActions.push_back(pAction);
		return;
	}

	m_vpRedoActions.clear();

	if((int)m_vpUndoActions.size() >= g_Config.m_ClEditorMaxHistory)
	{
		m_vpUndoActions.pop_front();
	}

	if(pDisplay == nullptr)
		m_vpUndoActions.emplace_back(pAction);
	else
		m_vpUndoActions.emplace_back(std::make_shared<CEditorActionBulk>(Map(), std::vector<std::shared_ptr<IEditorAction>>{pAction}, pDisplay));
}

bool CEditorHistory::Undo()
{
	if(m_vpUndoActions.empty())
		return false;

	auto pLastAction = m_vpUndoActions.back();
	m_vpUndoActions.pop_back();

	pLastAction->Undo();

	m_vpRedoActions.emplace_back(pLastAction);
	return true;
}

bool CEditorHistory::Redo()
{
	if(m_vpRedoActions.empty())
		return false;

	auto pLastAction = m_vpRedoActions.back();
	m_vpRedoActions.pop_back();

	pLastAction->Redo();

	m_vpUndoActions.emplace_back(pLastAction);
	return true;
}

void CEditorHistory::Clear()
{
	m_vpUndoActions.clear();
	m_vpRedoActions.clear();
}

void CEditorHistory::BeginBulk()
{
	m_IsBulk = true;
	m_vpBulkActions.clear();
}

void CEditorHistory::EndBulk(const char *pDisplay)
{
	if(!m_IsBulk)
		return;
	m_IsBulk = false;

	// Record bulk action
	if(!m_vpBulkActions.empty())
		RecordAction(std::make_shared<CEditorActionBulk>(Map(), m_vpBulkActions, pDisplay, true));

	m_vpBulkActions.clear();
}

void CEditorHistory::EndBulk(int DisplayToUse)
{
	EndBulk((DisplayToUse < 0 || DisplayToUse >= (int)m_vpBulkActions.size()) ? nullptr : m_vpBulkActions[DisplayToUse]->DisplayText());
}
