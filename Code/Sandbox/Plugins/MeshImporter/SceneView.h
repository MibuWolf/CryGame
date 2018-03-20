// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved.
#pragma once

#include <QAdvancedTreeView.h>

class CSceneModel;
class CComboBoxDelegate;

class QAttributeFilterProxyModel;
class QFilteringPanel;

class CSceneViewContainer;

class CSceneViewCommon : public QAdvancedTreeView
{
public:
	CSceneViewCommon(QWidget* pParent = nullptr);
	virtual ~CSceneViewCommon();

	void ExpandRecursive(const QModelIndex& index, bool bExpand);
	void ExpandParents(const QModelIndex& index);
};

class CSceneViewCgf : public CSceneViewCommon
{
public:
	CSceneViewCgf(QWidget* pParent = nullptr);
	virtual ~CSceneViewCgf();
private:
	std::unique_ptr<CComboBoxDelegate> m_pComboBoxDelegate;
};

class CSceneViewContainer : public QWidget
{
public:
	CSceneViewContainer(QAbstractItemModel* pModel, QTreeView* pView, QWidget* pParent = nullptr);

	const QAbstractItemModel*         GetModel() const;
	const QTreeView*				  GetView() const;
	const QAttributeFilterProxyModel* GetFilter() const;

	QAbstractItemModel*               GetModel();
	QAbstractItemView*                GetView();

private:
	std::unique_ptr<QAttributeFilterProxyModel> m_pFilterModel;
	QAbstractItemModel*					m_pModel;
	QTreeView*                          m_pView;
	QFilteringPanel* m_pFilteringPanel;
};
