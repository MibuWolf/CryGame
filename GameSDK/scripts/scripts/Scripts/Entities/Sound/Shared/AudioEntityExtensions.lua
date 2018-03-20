-- Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

Script.ReloadScript("Scripts/Entities/Sound/Shared/AudioUtils.lua");


--------------------------------------------------------------------------------------------------------
-- An entity extension providing functionality to execute audio triggers on animation "audio_trigger" event.
--------------------------------------------------------------------------------------------------------
AnimAudioEventHandling =
{
	perBoneAudioProxyUpdateInterval = 0.1,

	Client = {},
	Server = {},
}


-- Retrives auxiliary audio proxy ID for a bone with the given name. Returns nil on failure.
function AnimAudioEventHandling:GetAuxAudioProxyIDForBone(boneName)
	local state = self.AnimAudioEventHandlingState
	
	if (not boneName or boneName == "") then
		return nil
	end
	
	if (not state.proxies[boneName]) then
		state.proxies[boneName] = self:CreateAuxAudioProxy()
		
		local offset = {}
		SubVectors(offset, self:GetBonePos(boneName), self:GetPos())
		self:SetAudioProxyOffset(offset, state.proxies[boneName])
		
		--System.Log("AnimAudioEventHandling: created audio proxy for '" .. self:GetName() .. "' -> " .. boneName)
	end
	
	return state.proxies[boneName]	
end

-- OnSpawn hook
function AnimAudioEventHandling:OnSpawn()
	self.AnimAudioEventHandlingState =
	{
		updateCounter = 0.0,
		proxies = {}
	}
	
	--System.Log("AnimationAudioEventHandling: initialized for '" .. self:GetName() .. "'")
end

-- Client.OnAnimationEvent hook
function AnimAudioEventHandling.Client:OnAnimationEvent(eventType, eventData)
	if (eventType == "audio_trigger") then
		local proxy = self:GetAuxAudioProxyIDForBone(eventData.jointName) or self:GetDefaultAuxAudioProxyID()
		local trigger = AudioUtils.LookupTriggerID(eventData.customParam)
		if (proxy and trigger) then
	self:ExecuteAudioTrigger(trigger, proxy)
	--System.Log("AnimAudioEventHandling: executed audio trigger for '" .. self:GetName() .. "' -> " .. eventData.customParam .. " / " .. tostring(proxy))
		end
	end
end

-- Client.OnUpdate hook
function AnimAudioEventHandling.Client:OnUpdate(dt)
	local state = self.AnimAudioEventHandlingState

	if (not self:IsAnimationRunning(0,0)) then
		return
	end
	
	state.updateCounter = state.updateCounter - dt

	if (state.updateCounter <= 0.0) then
		state.updateCounter = self.perBoneAudioProxyUpdateInterval

		for boneName, proxy in pairs(state.proxies) do
	local offset = {}
	SubVectors(offset, self:GetBonePos(boneName), self:GetPos());
	self:SetAudioProxyOffset(offset, proxy);
	 
	--System.Log("AnimAudioEventHandling: updated audio proxy offset for '" .. self:GetName() .. "' -> " .. boneName .. " (ProxyID: " .. tostring(proxy) .. ")" .. " [" .. offset.x .. ", " .. offset.y .. ", " .. offset.z .. "]")
		end
	end
end

--------------------------------------------------------------------------------------------------------
