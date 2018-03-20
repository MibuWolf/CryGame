#include "StdAfx.h"
#include "CCryVKShaderReflection.hpp"
#include "../../../../../../../Code/Tools/HLSLCrossCompiler/src/reflect.inl"

HRESULT D3D10CreateBlob(size_t NumBytes, ID3DBlob** ppBuffer)
{
	*ppBuffer = new CCryVKBlob(NumBytes);
	return S_OK;
}

HRESULT D3DDisassemble(const void* pShader, size_t BytecodeLength, uint32 nFlags, ID3DBlob** ppComments, ID3DBlob** ppDisassembly)
{
	return S_OK;
}

HRESULT D3DCompile(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData, _In_ SIZE_T SrcDataSize, _In_opt_ LPCSTR pSourceName, CONST D3D_SHADER_MACRO* pDefines,
                   _In_opt_ ID3DInclude* pInclude, _In_opt_ LPCSTR pEntrypoint, _In_ LPCSTR pTarget, _In_ UINT Flags1, _In_ UINT Flags2, _Out_ ID3DBlob** ppCode,
                   _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorMsgs)
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}

HRESULT D3DReflect(const void* pShaderBytecode, size_t BytecodeLength, UINT pInterface, void** ppReflector)
{
	*ppReflector = new CCryVKShaderReflection(pShaderBytecode, BytecodeLength);
	return S_OK;
}

//////////////////////////////////////////////////////////////////////

CCryVKBlob::CCryVKBlob(size_t numBytes)
{
	m_pData = new uint8[numBytes];
	m_size = numBytes;
}

CCryVKBlob::~CCryVKBlob()
{
	delete[] m_pData;
}

//////////////////////////////////////////////////////////////////////

CCryVKShaderReflection::CCryVKShaderReflection(const void* pShaderBytecode, size_t BytecodeLength)
{
	CRY_ASSERT(pShaderBytecode && BytecodeLength > 0 && BytecodeLength % 4 == 0);

	const uint32* pByteCodeStart = reinterpret_cast<const uint32*>(pShaderBytecode);
	const uint32* pByteCodeEnd = pByteCodeStart + BytecodeLength / 4;

	m_pCompiler = stl::make_unique<spirv_cross::Compiler>(std::vector<uint32>(pByteCodeStart, pByteCodeEnd));
	m_shaderResources = m_pCompiler->get_shader_resources();

	// prepare input data
	if (m_pCompiler->get_execution_model() == spv::ExecutionModelVertex)
	{
		for (auto& input : m_shaderResources.stage_inputs)
		{
			SInputParameter inputParam;

			if (sscanf(input.name.c_str(), "dcl_Input%d_%[a-zA-Z]%d", &inputParam.attributeLocation, inputParam.semanticName, &inputParam.semanticIndex) == 3)
			{
				UnformatVariableName(inputParam.semanticName);
				m_shaderInputs.push_back(inputParam);
			}
		}
	}

	// NOTE: just for completeness. most of this stuff is untested!!
	for (auto pResourceList : { &m_shaderResources.sampled_images, &m_shaderResources.separate_images, &m_shaderResources.separate_samplers })
	{
		for (UINT localListIndex = 0; localListIndex < pResourceList->size(); ++localListIndex)
		{
			spirv_cross::Resource& resource = pResourceList->at(localListIndex);

			const spirv_cross::SPIRType& resourceType = m_pCompiler->get_type(resource.type_id);
			uint32 descriptorIndex = m_pCompiler->get_decoration(resource.base_type_id, spv::DecorationBinding);
			uint32 setIndex = m_pCompiler->get_decoration(resource.base_type_id, spv::DecorationDescriptorSet);

			struct isEqual
			{
				const char* p;
				isEqual(const char* s) : p(s) {}
				bool operator() ( const SResourceBinding& b) {
					return !strcmp(b.semanticName, p); }
			};

			// semanticType:
			// "ConstantBuffer";
			// "Texture";
			// "Sampler";
			// "Unordered";
			// "Buffer";
			char semanticTypeT[128];
			char semanticTypeS[128];
			SResourceBinding resourceBindingT;
			SResourceBinding resourceBindingS;

			resourceBindingT.semanticType = D3D10_SIT_TEXTURE;
			resourceBindingS.semanticType = D3D10_SIT_SAMPLER;

			if (sscanf(resource.name.c_str(), "dcl_%[a-zA-Z]%d_%[a-zA-Z]_%[a-zA-Z]%d_%[a-zA-Z]",
				semanticTypeT, &resourceBindingT.bindPoint, resourceBindingT.semanticName,
				semanticTypeS, &resourceBindingS.bindPoint, resourceBindingS.semanticName) == 6)
			{
				UnformatVariableName(resourceBindingT.semanticName);
				UnformatVariableName(resourceBindingS.semanticName);

				if (std::find_if(m_shaderBindings.begin(), m_shaderBindings.end(), isEqual(resourceBindingT.semanticName)) == m_shaderBindings.end())
					m_shaderBindings.push_back(resourceBindingT);
				if (std::find_if(m_shaderBindings.begin(), m_shaderBindings.end(), isEqual(resourceBindingS.semanticName)) == m_shaderBindings.end())
					m_shaderBindings.push_back(resourceBindingS);
			}

			else if (sscanf(resource.name.c_str(), "dcl_%[a-zA-Z]%d_%[a-zA-Z]",
				semanticTypeT, &resourceBindingT.bindPoint, resourceBindingT.semanticName) == 3)
			{
				UnformatVariableName(resourceBindingT.semanticName);

				if (std::find_if(m_shaderBindings.begin(), m_shaderBindings.end(), isEqual(resourceBindingT.semanticName)) == m_shaderBindings.end())
					m_shaderBindings.push_back(resourceBindingT);
			}
			else
			{
				SResourceBinding resourceBinding;
				resourceBinding.bindPoint = 0;
				strcpy(resourceBinding.semanticName, resource.name.c_str());
				switch (resourceType.basetype)
				{
				case spirv_cross::SPIRType::SampledImage:
				case spirv_cross::SPIRType::Sampler:
					resourceBinding.semanticType = D3D10_SIT_SAMPLER;
					break;
				case spirv_cross::SPIRType::Image:
					resourceBinding.semanticType = D3D10_SIT_TEXTURE;
					break;
				default:
					VK_NOT_IMPLEMENTED;
				}

				if (std::find_if(m_shaderBindings.begin(), m_shaderBindings.end(), isEqual(resourceBinding.semanticName)) == m_shaderBindings.end())
					m_shaderBindings.push_back(resourceBinding);
			}
		}
	}

	m_constantBuffers.resize(m_shaderResources.uniform_buffers.size());
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflection::GetDesc(D3D11_SHADER_DESC* pDesc)
{
	ZeroStruct(*pDesc);
	pDesc->ConstantBuffers = m_shaderResources.uniform_buffers.size();
	pDesc->InputParameters = m_shaderInputs.size();
	pDesc->BoundResources =  m_shaderBindings.size();

	return S_OK;
}

ID3D11ShaderReflectionConstantBuffer* STDMETHODCALLTYPE CCryVKShaderReflection::GetConstantBufferByIndex(UINT Index)
{
	CRY_ASSERT(Index < m_shaderResources.uniform_buffers.size());
	if (!m_constantBuffers[Index])
	{
		m_constantBuffers[Index].reset(new CCryVKShaderReflectionConstantBuffer(this, m_shaderResources.uniform_buffers[Index]));
	}
	return m_constantBuffers[Index].get();
}

ID3D11ShaderReflectionConstantBuffer* STDMETHODCALLTYPE CCryVKShaderReflection::GetConstantBufferByName(LPCSTR Name)
{
	VK_NOT_IMPLEMENTED
	return nullptr;
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflection::GetResourceBindingDesc(UINT ResourceIndex, D3D11_SHADER_INPUT_BIND_DESC* pDesc)
{
	CRY_ASSERT(ResourceIndex < m_shaderBindings.size());
	const auto& input = m_shaderBindings[ResourceIndex];

	ZeroStruct(*pDesc);
	pDesc->Name = input.semanticName;
	pDesc->Type = (D3D_SHADER_INPUT_TYPE)input.semanticType;
	pDesc->Dimension = D3D10_SRV_DIMENSION_UNKNOWN;
	pDesc->BindCount = 1;
	pDesc->BindPoint = input.bindPoint;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflection::GetInputParameterDesc(UINT ParameterIndex, D3D11_SIGNATURE_PARAMETER_DESC* pDesc)
{
	CRY_ASSERT(ParameterIndex < m_shaderInputs.size());
	const auto& input = m_shaderInputs[ParameterIndex];

	ZeroStruct(*pDesc);
	pDesc->SemanticName = input.semanticName;
	pDesc->SemanticIndex = input.semanticIndex;
	pDesc->ReadWriteMask = 0xFF;
	pDesc->AttributeLocation = input.attributeLocation;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflection::GetOutputParameterDesc(UINT ParameterIndex, D3D11_SIGNATURE_PARAMETER_DESC* pDesc)
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflection::GetPatchConstantParameterDesc(UINT ParameterIndex, D3D11_SIGNATURE_PARAMETER_DESC* pDesc)
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}

ID3D11ShaderReflectionVariable* STDMETHODCALLTYPE CCryVKShaderReflection::GetVariableByName(LPCSTR Name)
{
	VK_NOT_IMPLEMENTED
	return nullptr;
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflection::GetResourceBindingDescByName(LPCSTR Name, D3D11_SHADER_INPUT_BIND_DESC* pDesc)
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}

UINT STDMETHODCALLTYPE CCryVKShaderReflection::GetMovInstructionCount()
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}
UINT STDMETHODCALLTYPE CCryVKShaderReflection::GetMovcInstructionCount()
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}
UINT STDMETHODCALLTYPE CCryVKShaderReflection::GetConversionInstructionCount()
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}
UINT STDMETHODCALLTYPE CCryVKShaderReflection::GetBitwiseInstructionCount()
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}
D3D_PRIMITIVE STDMETHODCALLTYPE CCryVKShaderReflection::GetGSInputPrimitive()
{
	VK_NOT_IMPLEMENTED
	return D3D_PRIMITIVE_UNDEFINED;
}
BOOL STDMETHODCALLTYPE CCryVKShaderReflection::IsSampleFrequencyShader()
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}
UINT STDMETHODCALLTYPE CCryVKShaderReflection::GetNumInterfaceSlots()
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}
HRESULT STDMETHODCALLTYPE CCryVKShaderReflection::GetMinFeatureLevel(D3D_FEATURE_LEVEL* pLevel)
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}

//////////////////////////////////////////////////////////////////////

CCryVKShaderReflectionVariable::CCryVKShaderReflectionVariable(CCryVKShaderReflection* pShaderReflection, const spirv_cross::Resource& constantBuffer, uint32 memberIndex, bool bInUse)
	: m_pShaderReflection(pShaderReflection)
	, m_constantBuffer(constantBuffer)
	, m_memberIndex(memberIndex)
	, m_bInUse(bInUse)
{
	spirv_cross::Compiler& compiler = *pShaderReflection->m_pCompiler;

	const std::string& variableName = compiler.get_member_name(constantBuffer.base_type_id, memberIndex);

	strncpy(m_name, variableName.c_str() + 1 /* skip ShaderTypePrefix() */, sizeof(m_name));
	UnformatVariableName(m_name);
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflectionVariable::GetDesc(D3D11_SHADER_VARIABLE_DESC* pDesc)
{
	spirv_cross::Compiler& compiler = *m_pShaderReflection->m_pCompiler;
	const spirv_cross::SPIRType& structType = compiler.get_type(m_constantBuffer.type_id);

	ZeroStruct(*pDesc);
	pDesc->Name = m_name;
	pDesc->StartOffset = compiler.get_member_decoration(m_constantBuffer.base_type_id, m_memberIndex, spv::DecorationOffset);
	pDesc->Size = compiler.get_declared_struct_member_size(structType, m_memberIndex);
	pDesc->uFlags = m_bInUse ? D3D10_SVF_USED : 0;

	return S_OK;
}

ID3D11ShaderReflectionType* STDMETHODCALLTYPE CCryVKShaderReflectionVariable::GetType()
{
	if (!m_pType)
	{
		spirv_cross::Compiler& compiler = *m_pShaderReflection->m_pCompiler;
		const spirv_cross::SPIRType& structType = compiler.get_type(m_constantBuffer.type_id);

		CRY_ASSERT(m_memberIndex < structType.member_types.size());
		uint32_t memberTypeId = structType.member_types[m_memberIndex];

		m_pType.reset(new CCryVKShaderReflectionType(m_pShaderReflection, memberTypeId));
	}
	return m_pType.get();
}

ID3D11ShaderReflectionConstantBuffer* STDMETHODCALLTYPE CCryVKShaderReflectionVariable::GetBuffer()
{
	VK_NOT_IMPLEMENTED
	return nullptr;
}

UINT STDMETHODCALLTYPE CCryVKShaderReflectionVariable::GetInterfaceSlot(UINT uArrayIndex)
{
	VK_NOT_IMPLEMENTED
	return S_OK;
}

//////////////////////////////////////////////////////////////////////

CCryVKShaderReflectionType::CCryVKShaderReflectionType(CCryVKShaderReflection* pShaderReflection, uint32_t typeId)
{
	spirv_cross::Compiler& compiler = *pShaderReflection->m_pCompiler;
	const auto& memberType = compiler.get_type(typeId);

	ZeroStruct(m_Desc);

	switch (memberType.basetype)
	{
	case spirv_cross::SPIRType::Boolean:
	case spirv_cross::SPIRType::Char:
	case spirv_cross::SPIRType::Int:
	case spirv_cross::SPIRType::UInt:
	case spirv_cross::SPIRType::Int64:
	case spirv_cross::SPIRType::UInt64:
	case spirv_cross::SPIRType::Float:
	case spirv_cross::SPIRType::Double:
		m_Desc.Class = memberType.vecsize > 1 ? D3D10_SVC_VECTOR : D3D10_SVC_SCALAR;
		break;
	default:
		m_Desc.Class = D3D10_SVC_OBJECT;
		break;
	}
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflectionType::GetDesc(D3D11_SHADER_TYPE_DESC* pDesc)
{
	*pDesc = m_Desc;
	return S_OK;
}

//////////////////////////////////////////////////////////////////////

CCryVKShaderReflectionConstantBuffer::CCryVKShaderReflectionConstantBuffer(CCryVKShaderReflection* pShaderReflection, const spirv_cross::Resource& resource)
	: m_pShaderReflection(std::move(pShaderReflection))
	, m_resource(resource)
{
	spirv_cross::Compiler& compiler = *m_pShaderReflection->m_pCompiler;

	m_usedVariables = compiler.get_active_buffer_ranges(m_resource.id);

	std::string name = !m_resource.name.empty() ? m_resource.name : compiler.get_fallback_name(m_resource.base_type_id);
	int bindPoint = 0;

	if (sscanf(name.c_str(), "dcl_ConstantBuffer%d_%[a-zA-Z]", &bindPoint, m_name) == 2)
	{
		UnformatVariableName(m_name);
	}

	const spirv_cross::SPIRType& structType = compiler.get_type(m_resource.type_id);
	m_variables.resize(structType.member_types.size());
}

HRESULT STDMETHODCALLTYPE CCryVKShaderReflectionConstantBuffer::GetDesc(D3D11_SHADER_BUFFER_DESC* pDesc)
{
	spirv_cross::Compiler& compiler = *m_pShaderReflection->m_pCompiler;
	const spirv_cross::SPIRType& structType = compiler.get_type(m_resource.type_id);

	ZeroStruct(*pDesc);
	pDesc->Name = m_name;
	pDesc->Type = D3D_CT_CBUFFER;
	pDesc->Variables = structType.member_types.size();
	pDesc->Size = compiler.get_declared_struct_size(structType);

	return S_OK;
}

ID3D11ShaderReflectionVariable* STDMETHODCALLTYPE CCryVKShaderReflectionConstantBuffer::GetVariableByIndex(UINT Index)
{
	if (!m_variables[Index])
	{
		spirv_cross::Compiler& compiler = *m_pShaderReflection->m_pCompiler;
		const spirv_cross::SPIRType& structType = compiler.get_type(m_resource.type_id);

		CRY_ASSERT(Index < structType.member_types.size());

		bool bInUse = false;
		for (auto& range : m_usedVariables)
		{
			if (range.index == Index)
			{
				bInUse = true;
				break;
			}
		}

		m_variables[Index].reset(new CCryVKShaderReflectionVariable(m_pShaderReflection, m_resource, Index, bInUse));
	}

	return m_variables[Index].get();
}

ID3D11ShaderReflectionVariable* STDMETHODCALLTYPE CCryVKShaderReflectionConstantBuffer::GetVariableByName(LPCSTR Name)
{
	VK_NOT_IMPLEMENTED
	return nullptr;
}
