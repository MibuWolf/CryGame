// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include "AudioAssets.h"
#include "IUndoObject.h"
#include "AudioAssetsExplorerModel.h"

#include <QAbstractItemModel>
#include <QString>

class QStandardItem;

namespace ACE
{

typedef std::vector<int> TPath;

//-----------------------------------------
class IUndoControlOperation : public IUndoObject
{
protected:
	IUndoControlOperation() {}
	void AddStoredControl();
	void RemoveStoredControl();

	TPath                          m_path;
	CID                            m_id;
	std::shared_ptr<CAudioControl> m_pStoredControl;
};

class CUndoControlAdd : public IUndoControlOperation
{
public:
	explicit CUndoControlAdd(CID id);
protected:
	virtual int         GetSize() override        { return sizeof(*this); };
	virtual const char* GetDescription() override { return "Undo Control Add"; };

	virtual void        Undo(bool bUndo) override;
	virtual void        Redo() override;
};

class CUndoControlRemove : public IUndoControlOperation
{
public:
	explicit CUndoControlRemove(std::shared_ptr<CAudioControl>& pControl);
protected:
	virtual int         GetSize() override        { return sizeof(*this); };
	virtual const char* GetDescription() override { return "Undo Control Remove"; };

	virtual void        Undo(bool bUndo) override;
	virtual void        Redo() override;
};

//-----------------------------------------
class IUndoFolderOperation : public IUndoObject
{
protected:
	//explicit IUndoFolderOperation(QStandardItem* pItem);
	void AddFolderItem();
	void RemoveItem();

	TPath   m_path;
	QString m_sName;
};

class CUndoFolderRemove : public IUndoFolderOperation
{
public:
	//explicit CUndoFolderRemove(QStandardItem* pItem);
protected:
	virtual int         GetSize() override        { return sizeof(*this); };
	virtual const char* GetDescription() override { return "Undo Folder Remove"; };

	virtual void        Undo(bool bUndo) override;
	virtual void        Redo() override;
};

class CUndoFolderAdd : public IUndoFolderOperation
{
public:
	//explicit CUndoFolderAdd(QStandardItem* pItem);
protected:
	virtual int         GetSize() override        { return sizeof(*this); };
	virtual const char* GetDescription() override { return "Undo Folder Add"; };

	virtual void        Undo(bool bUndo) override;
	virtual void        Redo() override;
};

//-----------------------------------------
class CUndoControlModified : public IUndoObject
{
public:
	explicit CUndoControlModified(CID id);
protected:
	virtual int         GetSize() override        { return sizeof(*this); };
	virtual const char* GetDescription() override { return "Undo Control Changed"; };

	void                SwapData();
	virtual void        Undo(bool bUndo) override;
	virtual void        Redo() override;

	CID                        m_id;
	string                     m_name;
	Scope                      m_scope;
	bool                       m_bAutoLoad;
	std::vector<ConnectionPtr> m_connectedControls;
};

//-----------------------------------------
class CUndoItemMove : public IUndoObject
{
public:
	CUndoItemMove();

protected:
	virtual int         GetSize() override        { return sizeof(*this); };
	virtual const char* GetDescription() override { return "Undo Control Changed"; };

	virtual void        Undo(bool bUndo) override;
	virtual void        Redo() override;
	//void                Copy(QStandardItem* pSource, QStandardItem* pDest);

	/*QATLTreeModel m_original;
	   QATLTreeModel m_modified;*/

	bool bModifiedInitialised;
};
}
