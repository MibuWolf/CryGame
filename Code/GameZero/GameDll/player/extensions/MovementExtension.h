// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

class CMovementExtension : public CGameObjectExtensionHelper<CMovementExtension, ISimpleExtension>
{
public:
	// ISimpleExtension
	virtual void PostInit(IGameObject* pGameObject) override;
	virtual void PostUpdate(float frameTime) override;
	// ~ISimpleExtension

	// IEntityComponent
	virtual void OnShutDown() override;
	// ~IEntityComponent

	CMovementExtension();
	virtual ~CMovementExtension() {}

private:
	float m_movementSpeed;
	float m_boostMultiplier;
};
