#include "stdafx.h"
#include "dx10ConstantBuffer.h"

#include "dx10BufferUtils.h"
#include "../xrRender/dxRenderDeviceRender.h"

dx10ConstantBuffer::~dx10ConstantBuffer()
{
    if (dwFlags & xr_resource_flagged::RF_REGISTERED)
    {
        VERIFY(m_context_id < R__NUM_CONTEXTS);
        DEV->_DeleteConstantBuffer(m_context_id, this);
    }

    _RELEASE(m_pBuffer);
    xr_free(m_pBufferData);
}

dx10ConstantBuffer::dx10ConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable) : m_bChanged(true)
{
    D3D_SHADER_BUFFER_DESC Desc;

    R_CHK(pTable->GetDesc(&Desc));

    m_strBufferName = Desc.Name;
    m_eBufferType = Desc.Type;
    m_uiBufferSize = Desc.Size;

    //	Fill member list with variable descriptions
    m_MembersList.resize(Desc.Variables);
    m_MembersNames.resize(Desc.Variables);
    for (u32 i = 0; i < Desc.Variables; ++i)
    {
        ID3DShaderReflectionVariable* pVar;
        ID3DShaderReflectionType* pType;

        D3D_SHADER_VARIABLE_DESC var_desc;

        pVar = pTable->GetVariableByIndex(i);
        VERIFY(pVar);

        pType = pVar->GetType();
        VERIFY(pType);
        pType->GetDesc(&m_MembersList[i]);

        // Exclude pointers from hashing
        m_MembersList[i].Name = nullptr;

        //	Buffers with the same layout can contain totally different members
        R_CHK(pVar->GetDesc(&var_desc));
        m_MembersNames[i] = var_desc.Name;
    }

    m_uiMembersCRC = crc32(&m_MembersList[0], Desc.Variables * sizeof(m_MembersList[0]));

    R_CHK(dx10BufferUtils::CreateConstantBuffer(&m_pBuffer, Desc.Size));

    m_pBufferData = xr_malloc(Desc.Size);

    DXUT_SetDebugName(m_pBuffer, Desc.Name);
}

bool dx10ConstantBuffer::Similar(const dx10ConstantBuffer& _in) const
{
    if (!m_strBufferName.equal(_in.m_strBufferName))
        return false;

    if (m_eBufferType != _in.m_eBufferType)
        return false;

    if (m_uiMembersCRC != _in.m_uiMembersCRC)
        return false;

    return std::ranges::equal(m_MembersNames, _in.m_MembersNames);
}

void dx10ConstantBuffer::Flush(const u32 context_id)
{
    if (m_bChanged)
    {
        D3D11_MAPPED_SUBRESOURCE pSubRes;
        R_CHK(HW.get_context(context_id)->Map(m_pBuffer, 0, D3D_MAP_WRITE_DISCARD, 0, &pSubRes));
        CopyMemory(pSubRes.pData, m_pBufferData, m_uiBufferSize);
        HW.get_context(context_id)->Unmap(m_pBuffer, 0);
        m_bChanged = false;
    }
    //else
    //{
    //    Msg("skip buffer set [%s]", m_strBufferName.c_str());
    //}
}

void dx10ConstantBuffer::dbg_dump() const
{
    Msg("Buffer: %s", m_strBufferName.c_str());
    Msg("    Type: %d", m_eBufferType);
    Msg("    Size: %d", m_uiBufferSize);
    Msg("    Members: %d", m_MembersNames.size());
    for (u32 i = 0; i < m_MembersNames.size(); ++i)
    {
        Msg("        %s", m_MembersNames[i].c_str());
    }
}