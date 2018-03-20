// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "stdafx.h"
#include "Command_Commands.h"

#include "Helper.h"
#include "ControllerOpt.h"
#include "CharacterInstance.h"
#include "SkeletonAnim.h"
#include "SkeletonPose.h"
#include "Command_Buffer.h"
#include "Skeleton.h"
#include "LoaderDBA.h"
#include "CharacterManager.h"
#include "AttachmentBone.h"
#include "AttachmentPRow.h"
#include "PoseModifier/PoseModifierHelper.h"

extern float g_YLine;

namespace Command
{

void LoadControllers(const GlobalAnimationHeaderCAF& rGAH, const Command::CState& state, IController** controllers)
{
	memset(controllers, 0, state.m_jointCount * sizeof(IController*));

	if (rGAH.IsAssetOnDemand())
	{
		assert(rGAH.IsAssetLoaded());
		if (rGAH.IsAssetLoaded() == 0)
		{
			return;
			//	int nCurrentFrameID = g_pCharacterManager->m_nUpdateCounter;
			//	CryFatalError("CryAnimation: Asset Not Loaded: %s   nCurrentFrameID: %d  Weight: %f",rCAF.GetFilePath(),nCurrentFrameID,ac.m_fWeight);
		}
	}

	if (rGAH.m_nControllers2)
	{
		if (rGAH.m_nControllers == 0)
		{
			uint32 dba_exists = 0;
			if (rGAH.m_FilePathDBACRC32)
			{
				size_t numDBA_Files = g_AnimationManager.m_arrGlobalHeaderDBA.size();
				for (uint32 d = 0; d < numDBA_Files; d++)
				{
					CGlobalHeaderDBA& pGlobalHeaderDBA = g_AnimationManager.m_arrGlobalHeaderDBA[d];
					if (rGAH.m_FilePathDBACRC32 != pGlobalHeaderDBA.m_FilePathDBACRC32)
						continue;

					dba_exists++;
					break;
				}

			}

			if (dba_exists)
			{
				if (Console::GetInst().ca_DebugCriticalErrors)
				{
					//this case is virtually impossible, unless something went wrong with a DBA or maybe a CAF in a DBA was compressed to death and all controllers removed
					//	const char* mname = state.m_pInstance->GetFilePath();
					//	f32 fColor[4] = {1,1,0,1};
					//	g_pAuxGeom->Draw2dLabel( 1,g_YLine, 1.2f, fColor, false,"model: %s",mname);
					//	g_YLine+=0x10;
					//	g_pAuxGeom->Draw2dLabel( 1,g_YLine, 2.3f, fColor, false,"No Controllers found in Asset: %02x %08x %s",rGAH.m_nControllers2,rGAH.m_FilePathDBACRC32,rGAH.m_FilePath.c_str() );
					//	g_YLine+=23.0f;
					CryFatalError("CryAnimation: No Controllers found in Asset: %s", rGAH.GetFilePath());
				}

				g_pISystem->Warning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, VALIDATOR_FLAG_FILE, 0, "No Controllers found in Asset: %s", rGAH.GetFilePath());
			}

			return;  //return and don't play animation, because we don't have any controllers
		}
	}

	const uint32* pLodJointMask = NULL;
	uint32 lodJointMaskCount = 0;
	if (state.m_lod > 0)
	{
		if (uint32 lodCount = state.m_pDefaultSkeleton->m_arrAnimationLOD.size())
		{
			uint32 lod = state.m_lod;
			if (lod > lodCount)
				lod = lodCount;
			--lod;

			pLodJointMask = &state.m_pDefaultSkeleton->m_arrAnimationLOD[lod][0];
			lodJointMaskCount = state.m_pDefaultSkeleton->m_arrAnimationLOD[lod].size();
		}
	}

	const CDefaultSkeleton::SJoint* pModelJoint = &state.m_pDefaultSkeleton->m_arrModelJoints[0];
	uint32 jointCount = state.m_jointCount;
	for (uint32 i = 0; i < jointCount; ++i)
	{
		const uint32 crc32 = pModelJoint[i].m_nJointCRC32;
		if (pLodJointMask)
		{
			if (Helper::FindFromSorted(pLodJointMask, lodJointMaskCount, crc32) == NULL)
				continue;
		}

		if (!state.IsJointActive(crc32))
			continue;

		controllers[i] = rGAH.GetControllerByJointCRC32(pModelJoint[i].m_nJointCRC32);
	}
}

void ClearPoseBuffer::Execute(const CState& state, CEvaluationContext& context) const
{
	assert(m_TargetBuffer <= Command::TargetBuffer);

	const float identityConstant = m_nPoseInit ? 1.0f : 0.0f;

	//clear buffer for the relative pose
	const auto pRelPoseDst = static_cast<QuatT*>(context.m_buffers[m_TargetBuffer + 0]);
	const auto pScalingDst = static_cast<float*>(context.m_buffers[m_TargetBuffer + 3]);
	for (uint32 j = 0; j < state.m_jointCount; ++j)
	{
		pRelPoseDst[j].q.v.x = 0.0f;
		pRelPoseDst[j].q.v.y = 0.0f;
		pRelPoseDst[j].q.v.z = 0.0f;
		pRelPoseDst[j].q.w = identityConstant;
		pRelPoseDst[j].t.x = 0.0f;
		pRelPoseDst[j].t.y = 0.0f;
		pRelPoseDst[j].t.z = 0.0f;
		pScalingDst[j] = identityConstant;
	}

	//clear buffer joint states
	const auto pStatusDst = static_cast<JointState*>(context.m_buffers[m_TargetBuffer + 1]);
	for (uint32 j = 0; j < state.m_jointCount; ++j)
	{
		pStatusDst[j] = m_nJointStatus;
	}

	//clear buffer for weight mask (this is only used for partial body blends)
	const auto pWeightMask = static_cast<Vec3*>(context.m_buffers[m_TargetBuffer + 2]);
	if (pWeightMask)
	{
		for (uint32 j = 0; j < state.m_jointCount; ++j)
		{
			pWeightMask[j] = Vec3(ZERO);
		}
	}
}

//this function operates on "rGlobalAnimHeaderCAF"
void SampleAddAnimFull::Execute(const CState& state, CEvaluationContext& context) const
{

	const QuatT* const defaultPose = state.m_pDefaultSkeleton->m_poseDefaultData.GetJointsRelativeMain();
	const float* const defaultScale = state.m_pDefaultSkeleton->m_poseDefaultData.GetScalingRelative();

	const auto& rCAF = [&]() -> const GlobalAnimationHeaderCAF&
	{
		assert(m_nEAnimID >= 0);

		const ModelAnimationHeader* pMAG = state.m_pDefaultSkeleton->m_pAnimationSet->GetModelAnimationHeader(m_nEAnimID);
		assert(pMAG);
		assert(pMAG->m_nAssetType == CAF_File);
		assert(pMAG->m_nGlobalAnimId >= 0);

		return g_AnimationManager.m_arrGlobalCAF[pMAG->m_nGlobalAnimId];
	} ();
	assert(rCAF.IsAssetLoaded());

	PREFAST_SUPPRESS_WARNING(6255);
	const auto parrJointControllers = static_cast<IController**>(alloca(state.m_jointCount * sizeof(IController*)));
	LoadControllers(rCAF, state, parrJointControllers);

	const int32 bufferIndex = (m_flags & Flag_TmpBuffer) ? Command::TmpBuffer : Command::TargetBuffer;
	const auto outputRelPose = static_cast<QuatT*>(context.m_buffers[bufferIndex + 0]);
	const auto outputRelScale = static_cast<float*>(context.m_buffers[bufferIndex + 3]);
	const auto outputPoseState = static_cast<JointState*>(context.m_buffers[bufferIndex + 1]);

	const f32 keyTimeNew = rCAF.NTime2KTime(m_fETimeNew);
	const uint32 startingJointIndex = (m_flags & Flag_ADMotion) ? (1) : (0);

	if (startingJointIndex == 1)
	{
		// Root is always "identity" with animation-driven motions.
		// TODO: What should we do about the root scaling in this case?
		outputRelPose[0].SetIdentity();
		outputRelScale[0] = 1.0;
	}

	if (rCAF.IsAssetAdditive())
	{
		for (uint32 j = startingJointIndex; j < state.m_jointCount; ++j)
		{
			QuatT tempPose(defaultPose[j].q, defaultPose[j].t);
			Diag33 tempScale = Diag33(defaultScale ? defaultScale[j] : 1.0f);

			if (parrJointControllers[j])
			{
				const JointState ops = parrJointControllers[j]->GetOPS(keyTimeNew, tempPose.q, tempPose.t, tempScale);
				if (ops & eJS_Orientation)
				{
					tempPose.q *= defaultPose[j].q;
				}
				if (ops & eJS_Position)
				{
					tempPose.t += defaultPose[j].t;
				}
				if (ops & eJS_Scale)
				{
					tempScale *= Diag33(defaultScale ? defaultScale[j] : 1.0f);
					context.m_isScalingPresent = true;
				}

				tempPose.q *= fsgnnz(defaultPose[j].q | tempPose.q);
			}
			assert(tempPose.IsValid());

			outputRelPose[j].q += (tempPose.q * m_fWeight);
			outputRelPose[j].t += (tempPose.t * m_fWeight);
			outputRelScale[j] += (tempScale.x * m_fWeight);
		}
	}
	else
	{
		const QuatT* parrHemispherePose = Console::GetInst().ca_SampleQuatHemisphereFromCurrentPose ? outputRelPose : defaultPose; // joints to compare with in quaternion dot product
		for (uint32 j = startingJointIndex; j < state.m_jointCount; ++j)
		{
			QuatT tempPose(defaultPose[j].q, defaultPose[j].t);
			Diag33 tempScale = Diag33(defaultScale ? defaultScale[j] : 1.0f);

			if (parrJointControllers[j])
			{
				const JointState ops = parrJointControllers[j]->GetOPS(keyTimeNew, tempPose.q, tempPose.t, tempScale);

				if (ops & eJS_Scale)
				{
					context.m_isScalingPresent = true;
				}

				tempPose.q *= fsgnnz(parrHemispherePose[j].q | tempPose.q);
			}

			assert(tempPose.IsValid());

			outputRelPose[j].q += (tempPose.q * m_fWeight);
			outputRelPose[j].t += (tempPose.t * m_fWeight);
			outputRelScale[j] += (tempScale.x * m_fWeight);
		}
	}

	static_assert(sizeof(*outputPoseState) == 1, "Invalid assumption on the size of the joint state descriptor!");
	memset(outputPoseState, (eJS_Position | eJS_Orientation | eJS_Scale), state.m_jointCount); // If we have an animation in the base-layer, then we ALWAYS initialize all joints.

	g_pCharacterManager->g_SkeletonUpdates++;

	std::for_each(outputRelPose, outputRelPose + state.m_jointCount, [](const QuatT& x)
		{
			assert(x.q.IsValid());
			assert(x.t.IsValid());
	  });

	std::for_each(outputRelScale, outputRelScale + state.m_jointCount, [](const float& x)
		{
			assert(NumberValid(x));
	  });
}

//this function operates on "rGlobalAnimHeaderAIM"
void SampleAddPoseFull::Execute(const CState& state, CEvaluationContext& context) const
{

	const CDefaultSkeleton::SJoint* const pModelJoint = state.m_pDefaultSkeleton->m_arrModelJoints.data();
	const QuatT* const parrDefJoints = state.m_pDefaultSkeleton->m_poseDefaultData.GetJointsRelativeMain();
	const float* const parrDefScaling = state.m_pDefaultSkeleton->m_poseDefaultData.GetScalingRelative();
	const auto& rGlobalAnimHeaderAIM = [&]() -> const GlobalAnimationHeaderAIM&
	{
		assert(m_nEAnimID >= 0);
		const ModelAnimationHeader* pMAG = state.m_pDefaultSkeleton->m_pAnimationSet->GetModelAnimationHeader(m_nEAnimID);
		assert(pMAG);
		assert(pMAG->m_nAssetType == AIM_File);
		return g_AnimationManager.m_arrGlobalAIM[pMAG->m_nGlobalAnimId];

	} ();

	PREFAST_SUPPRESS_WARNING(6255)
	const auto parrController = static_cast<IController**>(alloca(state.m_jointCount * sizeof(IController*)));
	memset(parrController, 0, state.m_jointCount * sizeof(IController*));
	for (uint32 i = 0; i < state.m_jointCount; ++i)
	{
		parrController[i] = rGlobalAnimHeaderAIM.GetControllerByJointCRC32(pModelJoint[i].m_nJointCRC32);
	}

	const int32 nBufferID = (m_flags& SampleAddAnimFull::Flag_TmpBuffer) ? Command::TmpBuffer : Command::TargetBuffer;
	const auto parrRelJointsDst = static_cast<QuatT*>(context.m_buffers[nBufferID + 0]);
	const auto parrRelScalingDst = static_cast<float*>(context.m_buffers[nBufferID + 3]);
	const auto parrStatusDst = static_cast<JointState*>(context.m_buffers[nBufferID + 1]);
	const QuatT* parrHemispherePose = Console::GetInst().ca_SampleQuatHemisphereFromCurrentPose ? parrRelJointsDst : parrDefJoints; // joints to compare with in quaternion dot product

	const f32 fKeyTimeNew = rGlobalAnimHeaderAIM.NTime2KTime(m_fETimeNew);
	for (uint32 j = 0; j < state.m_jointCount; ++j)
	{
		Quat rot;
		Vec3 pos;
		Diag33 scl;
		if (parrController[j])
		{
			const JointState jointState = parrController[j]->GetOPS(fKeyTimeNew, rot, pos, scl);

			if (jointState & eJS_Scale)
			{
				context.m_isScalingPresent = true;
			}

			rot *= fsgnnz(parrHemispherePose[j].q | rot); // TODO: This could be optimized at loading-time
		}
		else
		{
			rot = parrDefJoints[j].q;
			pos = parrDefJoints[j].t;
			scl = Diag33(parrDefScaling ? parrDefScaling[j] : 1.0f);
		}
		assert(rot.IsUnit());
		assert(rot.IsValid());
		assert(pos.IsValid());
		assert(scl.IsValid());

		parrRelJointsDst[j].q += m_fWeight * rot;
		parrRelJointsDst[j].t += m_fWeight * pos;
		parrRelScalingDst[j] += m_fWeight * scl.x;
	}

	static_assert(sizeof(*parrStatusDst) == 1, "Invalid assumption on the size of the joint state descriptor!");
	memset(parrStatusDst, (eJS_Position | eJS_Orientation | eJS_Scale), state.m_jointCount); // See comment in SampleAddAnimFull::Execute().

	std::for_each(parrRelJointsDst, parrRelJointsDst + state.m_jointCount, [](const QuatT& x)
		{
			assert(x.q.IsValid());
			assert(x.t.IsValid());
	  });

	std::for_each(parrRelScalingDst, parrRelScalingDst + state.m_jointCount, [](const float& x)
		{
			assert(NumberValid(x));
	  });
}

//reads content from m_SourceBuffer, multiplies the pose by a blend weight, and adds the result to the m_TargetBuffer
void AddPoseBuffer::Execute(const CState& state, CEvaluationContext& context) const
{
	const AddPoseBuffer& ac = *this;
	void** CBTemp = context.m_buffers;

	assert(ac.m_SourceBuffer <= Command::TargetBuffer);
	QuatT* parrRelPoseSrc = (QuatT*)  CBTemp[ac.m_SourceBuffer + 0];
	JointState* parrStatusSrc = (JointState*)CBTemp[ac.m_SourceBuffer + 1];

	assert(ac.m_TargetBuffer <= Command::TargetBuffer);
	QuatT* parrRelPoseDst = (QuatT*)  CBTemp[ac.m_TargetBuffer + 0];
	JointState* parrStatusDst = (JointState*)CBTemp[ac.m_TargetBuffer + 1];

	f32 t = ac.m_fWeight;

	uint32 numJoints = state.m_jointCount;
	for (uint32 i = 0; i < numJoints; i++)
	{
		parrRelPoseDst[i].q += parrRelPoseSrc[i].q * t;
		parrRelPoseDst[i].t += parrRelPoseSrc[i].t * t;
		parrStatusDst[i] |= parrStatusSrc[i];
	}
#ifdef _DEBUG
	for (uint32 j = 0; j < numJoints; j++)
	{
		assert(parrRelPoseDst[j].q.IsValid());
		assert(parrRelPoseDst[j].t.IsValid());
	}
#endif
}

void NormalizeFull::Execute(const CState& state, CEvaluationContext& context) const
{
	const NormalizeFull& ac = *this;
	void** CBTemp = context.m_buffers;

	assert(ac.m_TargetBuffer <= Command::TargetBuffer);
	QuatT* parrRelPoseDst = (QuatT*)  CBTemp[ac.m_TargetBuffer + 0];
	JointState* parrStatusDst = (JointState*)CBTemp[ac.m_TargetBuffer + 1];

	f32 fDotLocator = fabsf(parrRelPoseDst[0].q | parrRelPoseDst[0].q);
	if (fDotLocator > 0.0001f)
		parrRelPoseDst[0].q *= isqrt_tpl(fDotLocator);
	else
		parrRelPoseDst[0].q.SetIdentity();

	uint32 numJoints = state.m_jointCount;
	for (uint32 i = 1; i < numJoints; i++)
	{
		f32 dot = fabsf(parrRelPoseDst[i].q | parrRelPoseDst[i].q);
		if (dot > 0.0001f)
			parrRelPoseDst[i].q *= isqrt_tpl(dot);
		else
			parrRelPoseDst[i].q.SetIdentity();
		assert(parrRelPoseDst[i].q.IsUnit());
	}
}

void ScaleUniformFull::Execute(const CState& state, CEvaluationContext& context) const
{
	assert(m_TargetBuffer <= Command::TargetBuffer);

	const auto parrRelPoseDst = static_cast<QuatT*>(context.m_buffers[m_TargetBuffer + 0]);
	const auto parrStatusDst = static_cast<JointState*>(context.m_buffers[m_TargetBuffer + 1]);

	for (uint32 j = 0; j < state.m_jointCount; ++j)
	{
		if (parrStatusDst[j] & eJS_Orientation) // TODO: This check doesn't make sense, why is it performed?
		{
			parrRelPoseDst[j].t *= m_fScale;
		}
	}

	std::for_each(parrRelPoseDst, parrRelPoseDst + state.m_jointCount, [](const QuatT& x)
		{
			assert(x.q.IsValid());
			assert(x.t.IsValid());
	  });
}

#if defined(USE_PROTOTYPE_ABS_BLENDING)
struct SAbsoluteTransform
{
	Quat prevAbs;
	Quat newAbs;
};
#endif //!(USE_PROTOTYPE_ABS_BLENDING)

void SampleAddAnimPart::Execute(const CState& state, CEvaluationContext& context) const
{

	assert(m_TargetBuffer <= Command::TargetBuffer);
	const auto parrRelPoseDst = static_cast<QuatT*>(context.m_buffers[m_TargetBuffer + 0]);
	const auto parrScalingDst = static_cast<float*>(context.m_buffers[m_TargetBuffer + 3]);
	const auto parrStatusDst = static_cast<JointState*>(context.m_buffers[m_TargetBuffer + 1]);
	const auto parrJWeightsDst = static_cast<Vec3*>(context.m_buffers[m_TargetBuffer + 2]);
	if (!parrJWeightsDst)
	{
		return;
	}

	const auto& rCAF = [&]() -> const GlobalAnimationHeaderCAF& // TODO: move outside
	{
		assert(m_nEAnimID >= 0);

		const ModelAnimationHeader* pMAG = state.m_pDefaultSkeleton->m_pAnimationSet->GetModelAnimationHeader(m_nEAnimID);
		assert(pMAG);
		assert(pMAG->m_nAssetType == CAF_File);
		assert(pMAG->m_nGlobalAnimId >= 0);

		return g_AnimationManager.m_arrGlobalCAF[pMAG->m_nGlobalAnimId];
	} ();
	assert(rCAF.IsAssetLoaded());

	PREFAST_SUPPRESS_WARNING(6255)
	const auto parrController = static_cast<IController**>(alloca(state.m_jointCount * sizeof(IController*)));
	LoadControllers(rCAF, state, parrController);

	assert(m_fAnimTime >= 0.0f && m_fAnimTime <= 1.0f);
	const f32 fKeyTimeNew = rCAF.NTime2KTime(m_fAnimTime);

	if (rCAF.IsAssetAdditive())
	{
		for (uint32 j = 1; j < state.m_jointCount; ++j)
		{
			if (parrController[j])
			{
				Quat rot;
				Vec3 pos;
				Diag33 scl;
				const JointState jointState = parrController[j]->GetOPS(fKeyTimeNew, rot, pos, scl);

				if (jointState & eJS_Orientation)
				{
					parrJWeightsDst[j].x += m_fWeight;
					parrRelPoseDst[j].q = Quat::CreateNlerp(IDENTITY, rot, m_fWeight) * parrRelPoseDst[j].q;
				}
				if (jointState & eJS_Position)
				{
					parrJWeightsDst[j].y += m_fWeight;
					parrRelPoseDst[j].t += pos * m_fWeight;
				}
				if (jointState & eJS_Scale)
				{
					parrJWeightsDst[j].z += m_fWeight;
					parrScalingDst[j] *= LERP(1.0f, scl.x, m_fWeight);
					context.m_isScalingPresent = true;
				}

				parrStatusDst[j] |= jointState;
			}
		}
	}
	else
	{
		const QuatT* parrDefJoints = state.m_pDefaultSkeleton->m_poseDefaultData.GetJointsRelativeMain();
		const QuatT* parrHemispherePose = Console::GetInst().ca_SampleQuatHemisphereFromCurrentPose ? parrRelPoseDst : parrDefJoints; // joints to compare with in quaternion dot product

		for (uint32 j = 1; j < state.m_jointCount; ++j)
		{
			if (parrController[j])
			{
				Quat rot;
				Vec3 pos;
				Diag33 scl;
				const JointState jointState = parrController[j]->GetOPS(fKeyTimeNew, rot, pos, scl);

				if (jointState & eJS_Orientation)
				{
					parrJWeightsDst[j].x += m_fWeight;
					parrRelPoseDst[j].q += rot * fsgnnz(parrHemispherePose[j].q | rot) * m_fWeight;
				}
				if (jointState & eJS_Position)
				{
					parrJWeightsDst[j].y += m_fWeight;
					parrRelPoseDst[j].t += pos * m_fWeight;
				}
				if (jointState & eJS_Scale)
				{
					parrJWeightsDst[j].z += m_fWeight;
					parrScalingDst[j] += scl.x * m_fWeight; // Take the x component only, we don't support non-uniform scaling yet.
					context.m_isScalingPresent = true;
				}

				parrStatusDst[j] |= jointState;
			}
		}
	}

	g_pCharacterManager->g_SkeletonUpdates++;

#ifdef _DEBUG
	{
		uint32 o = 0;
		uint32 p = 0;
		uint32 s = 0;
		for (uint32 j = 0; j < state.m_jointCount; ++j)
		{
			if (parrStatusDst[j] & eJS_Orientation)
			{
				const Quat rot = parrRelPoseDst[j].q;
				assert(parrRelPoseDst[j].q.IsValid());
				o++;
			}
			if (parrStatusDst[j] & eJS_Position)
			{
				const Vec3 pos = parrRelPoseDst[j].t;
				assert(parrRelPoseDst[j].t.IsValid());
				p++;
			}
			if (parrStatusDst[j] & eJS_Scale)
			{
				const float scl = parrScalingDst[j];
				assert(NumberValid(parrScalingDst[j]));
				s++;
			}
		}
	}
#endif

}

void PerJointBlending::Execute(const CState& state, CEvaluationContext& context) const
{
	assert(m_TargetBuffer <= Command::TargetBuffer);

	// This is source-buffer No.1
	const auto parrRelPoseLayer = static_cast<QuatT*>(context.m_buffers[m_SourceBuffer + 0]);
	const auto parrScalingLayer = static_cast<float*>(context.m_buffers[m_SourceBuffer + 3]);
	const auto parrStatusLayer = static_cast<JointState*>(context.m_buffers[m_SourceBuffer + 1]);
	const auto parrWeightsLayer = static_cast<Vec3*>(context.m_buffers[m_SourceBuffer + 2]);
	if (!parrWeightsLayer)
	{
		return;
	}

	// This is Source-Buffer No.2 and also the Target-Buffer
	const auto parrRelPoseBase = static_cast<QuatT*>(context.m_buffers[m_TargetBuffer + 0]);
	const auto parrScalingBase = static_cast<float*>(context.m_buffers[m_TargetBuffer + 3]);

	if (m_BlendMode)
	{
		for (uint32 j = 0; j < state.m_jointCount; ++j)
		{
			// Additive Blending
			if (parrStatusLayer[j] & eJS_Orientation)
			{
				parrRelPoseBase[j].q = Quat::CreateNlerp(IDENTITY, parrRelPoseLayer[j].q, parrWeightsLayer[j].x) * parrRelPoseBase[j].q;
			}
			if (parrStatusLayer[j] & eJS_Position)
			{
				parrRelPoseBase[j].t += parrRelPoseLayer[j].t * parrWeightsLayer[j].y;
			}
			if (parrStatusLayer[j] & eJS_Scale)
			{
				parrScalingBase[j] *= LERP(1.0f, parrScalingLayer[j], parrWeightsLayer[j].z);
			}
		}
	}
	else
	{
		for (uint32 j = 0; j < state.m_jointCount; ++j)
		{
			// Override Blending
			if (parrStatusLayer[j] & eJS_Orientation)
			{
				parrRelPoseBase[j].q = (parrRelPoseBase[j].q * (1.0f - parrWeightsLayer[j].x) + parrRelPoseLayer[j].q * fsgnnz(parrRelPoseBase[j].q | parrRelPoseLayer[j].q)).GetNormalized(); // Quaternion LERP
			}
			if (parrStatusLayer[j] & eJS_Position)
			{
				parrRelPoseBase[j].t = parrRelPoseBase[j].t * (1.0f - parrWeightsLayer[j].y) + parrRelPoseLayer[j].t;  // Vector LERP
			}
			if (parrStatusLayer[j] & eJS_Scale)
			{
				parrScalingBase[j] = parrScalingBase[j] * (1.0f - parrWeightsLayer[j].y) + parrScalingLayer[j];
			}
		}
	}
}

void PoseModifier::Execute(const CState& state, CEvaluationContext& context) const
{
	const PoseModifier& ac = *this;
	void** CBTemp = context.m_buffers;

	SAnimationPoseModifierParams params;
	params.pCharacterInstance = state.m_pInstance;
	params.pPoseData = state.m_pPoseData;
	params.timeDelta = state.m_originalTimeDelta;
	params.location = state.m_location;
	ac.m_pPoseModifier->Execute(params);
	/*
	   #ifdef _DEBUG
	   for (uint32 j=0; j<params.jointCount; j++)
	   {
	    assert( params.pPoseRelative[j].q.IsUnit() );
	    assert( params.pPoseAbsolute[j].q.IsUnit() );
	    assert( params.pPoseRelative[j].IsValid() );
	    assert( params.pPoseAbsolute[j].IsValid() );
	   }
	   #endif
	 */
}

void UpdateRedirectedJoint::Execute(const CState& state, CEvaluationContext& context) const
{
	m_attachmentBone->Update_Redirected(*state.m_pPoseData);
}

void UpdatePendulumRow::Execute(const CState& state, CEvaluationContext& context) const
{
	m_attachmentPendulumRow->UpdateRow(*state.m_pPoseData);
}

void PrepareAllRedirectedTransformations::Execute(const CState& state, CEvaluationContext& context) const
{
	state.m_pInstance->m_AttachmentManager.PrepareAllRedirectedTransformations(*state.m_pPoseData);
}

void GenerateProxyModelRelativeTransformations::Execute(const CState& state, CEvaluationContext& context) const
{
	state.m_pInstance->m_AttachmentManager.GenerateProxyModelRelativeTransformations(*state.m_pPoseData);
}

void ComputeAbsolutePose::Execute(const CState& state, CEvaluationContext& context) const
{
	state.m_pPoseData->ValidateRelative(*state.m_pInstance->m_pDefaultSkeleton);
	state.m_pPoseData->ComputeAbsolutePose(*state.m_pInstance->m_pDefaultSkeleton, state.m_pDefaultSkeleton->m_ObjectType == CHR);
	state.m_pPoseData->ValidateAbsolute(*state.m_pInstance->m_pDefaultSkeleton);
}

void ProcessAnimationDrivenIkFunction(CCharInstance& instance, IAnimationPoseData* pAnimationPoseData, const QuatTS location)
{
	DEFINE_PROFILER_FUNCTION();

	Skeleton::CPoseData* pPoseData = Skeleton::CPoseData::GetPoseData(pAnimationPoseData);
	if (!pPoseData)
		return;

	int jointCount = int(pPoseData->GetJointCount());

	const CDefaultSkeleton& modelSkeleton = *instance.m_pDefaultSkeleton;

	QuatT* const __restrict pPoseRelative = pPoseData->GetJointsRelative();
	QuatT* const __restrict pPoseAbsolute = pPoseData->GetJointsAbsolute();

	uint targetCount = uint(modelSkeleton.m_ADIKTargets.size());
	for (uint i = 0; i < targetCount; i++)
	{
		LimbIKDefinitionHandle nHandle = modelSkeleton.m_ADIKTargets[i].m_nHandle;

		int targetJointIndex = modelSkeleton.m_ADIKTargets[i].m_idxTarget;
		assert(targetJointIndex < (int)jointCount);
		if (targetJointIndex < 0)
			continue;

		int targetWeightJointIndex = modelSkeleton.m_ADIKTargets[i].m_idxWeight;
		assert(targetWeightJointIndex < (int)jointCount);
		if (targetWeightJointIndex < 0)
			continue;

		int32 limbDefinitionIndex = modelSkeleton.GetLimbDefinitionIdx(nHandle);
		if (limbDefinitionIndex < 0)
			continue;

		const IKLimbType& limbType = modelSkeleton.m_IKLimbTypes[limbDefinitionIndex];

		const QuatT& targetLocationAbsolute = pPoseAbsolute[targetJointIndex];
		float targetBlendWeight = pPoseRelative[targetWeightJointIndex].t.x;
		targetBlendWeight = clamp_tpl(targetBlendWeight, 0.0f, 1.0f);
		if (targetBlendWeight > 0.01f)
		{
			uint numLinks = uint(limbType.m_arrJointChain.size());
			int endEffectorJointIndex = limbType.m_arrJointChain[numLinks - 1].m_idxJoint;
			int endEffectorParentJointIndex = modelSkeleton.m_arrModelJoints[endEffectorJointIndex].m_idxParent;

			pPoseAbsolute[0] = pPoseRelative[0];
			ANIM_ASSET_ASSERT(pPoseRelative[0].q.IsUnit());
			for (uint j = 1; j < numLinks; ++j)
			{
				int p = limbType.m_arrRootToEndEffector[j - 1];
				int c = limbType.m_arrRootToEndEffector[j];
				ANIM_ASSET_ASSERT(pPoseRelative[c].q.IsUnit());
				ANIM_ASSET_ASSERT(pPoseRelative[p].q.IsUnit());
				pPoseAbsolute[c] = pPoseAbsolute[p] * pPoseRelative[c];
				ANIM_ASSET_ASSERT(pPoseRelative[c].q.IsUnit());
			}

			const char* _2BIK = "2BIK";
			const char* _3BIK = "3BIK";
			const char* _CCDX = "CCDX";
			Vec3 vLocalTarget;
			vLocalTarget.SetLerp(pPoseAbsolute[endEffectorJointIndex].t, targetLocationAbsolute.t, targetBlendWeight);
			if (limbType.m_nSolver == *(uint32*)_2BIK)
				PoseModifierHelper::IK_Solver2Bones(vLocalTarget, limbType, *pPoseData);
			if (limbType.m_nSolver == *(uint32*)_3BIK)
				PoseModifierHelper::IK_Solver3Bones(vLocalTarget, limbType, *pPoseData);
			if (limbType.m_nSolver == *(uint32*)_CCDX)
				PoseModifierHelper::IK_SolverCCD(vLocalTarget, limbType, *pPoseData);

			for (uint j = 1; j < numLinks; ++j)
			{
				int c = limbType.m_arrJointChain[j].m_idxJoint;
				int p = limbType.m_arrJointChain[j - 1].m_idxJoint;
				pPoseAbsolute[c] = pPoseAbsolute[p] * pPoseRelative[c];
				ANIM_ASSET_ASSERT(pPoseAbsolute[c].q.IsUnit());
			}

#ifdef _DEBUG
			ANIM_ASSET_ASSERT(targetLocationAbsolute.q.IsUnit());
			ANIM_ASSET_ASSERT(targetLocationAbsolute.q.IsValid());
			ANIM_ASSET_ASSERT(pPoseAbsolute[endEffectorJointIndex].q.IsUnit());
			ANIM_ASSET_ASSERT(pPoseAbsolute[endEffectorJointIndex].q.IsValid());
#endif
			pPoseAbsolute[endEffectorJointIndex].q.SetNlerp(
			  pPoseAbsolute[endEffectorJointIndex].q, targetLocationAbsolute.q, targetBlendWeight);
#ifdef _DEBUG
			ANIM_ASSET_ASSERT(pPoseAbsolute[endEffectorParentJointIndex].q.IsUnit());
			ANIM_ASSET_ASSERT(pPoseAbsolute[endEffectorParentJointIndex].q.IsValid());
			ANIM_ASSET_ASSERT(pPoseAbsolute[endEffectorJointIndex].q.IsUnit());
			ANIM_ASSET_ASSERT(pPoseAbsolute[endEffectorJointIndex].q.IsValid());
#endif
			pPoseRelative[endEffectorJointIndex].q = !pPoseAbsolute[endEffectorParentJointIndex].q * pPoseAbsolute[endEffectorJointIndex].q;

			uint numChilds = modelSkeleton.m_IKLimbTypes[limbDefinitionIndex].m_arrLimbChildren.size();
			const int16* pJointIdx = &modelSkeleton.m_IKLimbTypes[limbDefinitionIndex].m_arrLimbChildren[0];
			for (uint u = 0; u < numChilds; u++)
			{
				int c = pJointIdx[u];
				int p = modelSkeleton.m_arrModelJoints[c].m_idxParent;
				pPoseAbsolute[c] = pPoseAbsolute[p] * pPoseRelative[c];
#ifdef _DEBUG
				ANIM_ASSET_ASSERT(pPoseAbsolute[c].q.IsUnit());
				ANIM_ASSET_ASSERT(pPoseAbsolute[p].q.IsUnit());
				ANIM_ASSET_ASSERT(pPoseRelative[c].q.IsUnit());
				ANIM_ASSET_ASSERT(pPoseAbsolute[c].q.IsValid());
				ANIM_ASSET_ASSERT(pPoseAbsolute[p].q.IsValid());
				ANIM_ASSET_ASSERT(pPoseRelative[c].q.IsValid());
#endif
			}
		}

		if (Console::GetInst().ca_DebugADIKTargets)
		{
			const char* pName = modelSkeleton.m_ADIKTargets[i].m_strTarget;

			float fColor[4] = { 0, 1, 0, 1 };
			g_pAuxGeom->Draw2dLabel(1, g_YLine, 1.4f, fColor, false, "LHand_IKTarget: name: %s  rot: (%f %f %f %f)  pos: (%f %f %f) blend: %f", pName, targetLocationAbsolute.q.w, targetLocationAbsolute.q.v.x, targetLocationAbsolute.q.v.y, targetLocationAbsolute.q.v.z, targetLocationAbsolute.t.x, targetLocationAbsolute.t.y, targetLocationAbsolute.t.z, pPoseRelative[targetWeightJointIndex].t.x);
			g_YLine += 16.0f;

			static Ang3 angle(0, 0, 0);
			angle += Ang3(0.01f, +0.02f, +0.03f);
			AABB aabb = AABB(Vec3(-0.015f, -0.015f, -0.015f), Vec3(+0.015f, +0.015f, +0.015f));

			Matrix33 m33 = Matrix33::CreateRotationXYZ(angle);
			OBB obb = OBB::CreateOBBfromAABB(m33, aabb);
			Vec3 obbPos = location * targetLocationAbsolute.t;
			g_pAuxGeom->DrawOBB(obb, obbPos, 1, RGBA8(0xff, 0x00, 0x1f, 0xff), eBBD_Extremes_Color_Encoded);
		}
	}
}

void ProcessAnimationDrivenIk::Execute(const CState& state, CEvaluationContext& context) const
{
	if (!state.m_pInstance->m_SkeletonPose.m_physics.m_bPhysicsRelinquished && state.m_pInstance->m_SkeletonAnim.m_IsAnimPlaying && Console::GetInst().ca_useADIKTargets)
	{
		ProcessAnimationDrivenIkFunction(*state.m_pInstance, state.m_pPoseData, state.m_location);
	}
}

void PhysicsSync::Execute(const CState& state, CEvaluationContext& context) const
{
	state.m_pInstance->m_SkeletonPose.m_physics.Job_Physics_SynchronizeFrom(*state.m_pPoseData, state.m_originalTimeDelta);
}

#ifdef _DEBUG
void VerifyFull::Execute(const CState& state, CEvaluationContext& context) const
{
	const VerifyFull& ac = *this;
	void** CBTemp = context.m_buffers;

	assert(ac.m_TargetBuffer <= Command::TargetBuffer);
	QuatT* parrRelPoseDst = (QuatT*)    CBTemp[ac.m_TargetBuffer + 0];
	JointState* parrStatusDst = (JointState*)  CBTemp[ac.m_TargetBuffer + 1];
	uint32 numJoints = state.m_jointCount;
	for (uint32 j = 0; j < numJoints; j++)
	{
		JointState s4 = parrStatusDst[j];
		assert(parrRelPoseDst[j].q.IsValid());
		assert(parrRelPoseDst[j].t.IsValid());
	}
}
#endif

} // namespace Command
