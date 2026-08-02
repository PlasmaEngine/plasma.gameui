#include <GameUiPlugin/GameUiPluginPCH.h>

#include <Foundation/Serialization/ReflectionSerializer.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <GameUiPlugin/Resources/UiDocumentResource.h>

#include <Core/ResourceManager/ResourceManager.h>

static plTypeVersion s_UiDocumentDescVersion = 2;

plUiDocumentResourceDescriptor::plUiDocumentResourceDescriptor() = default;

plUiDocumentResourceDescriptor::plUiDocumentResourceDescriptor(plUiDocumentResourceDescriptor&& other)
{
  *this = std::move(other);
}

plUiDocumentResourceDescriptor::~plUiDocumentResourceDescriptor()
{
  Clear();
}

void plUiDocumentResourceDescriptor::operator=(plUiDocumentResourceDescriptor&& rhs)
{
  Clear();

  m_pRoot = rhs.m_pRoot;
  rhs.m_pRoot = nullptr;

  m_Clips = std::move(rhs.m_Clips);
  rhs.m_Clips.Clear();

  m_sStyle = std::move(rhs.m_sStyle);
  rhs.m_sStyle.Clear();
}

void plUiDocumentResourceDescriptor::Clear()
{
  if (m_pRoot != nullptr)
  {
    PL_DEFAULT_DELETE(m_pRoot);
    m_pRoot = nullptr;
  }

  for (plUiAnimationClip* pClip : m_Clips)
  {
    PL_DEFAULT_DELETE(pClip);
  }

  m_Clips.Clear();
  m_sStyle.Clear();
}

plResult plUiDocumentResourceDescriptor::Save(plStreamWriter& inout_stream)
{
  inout_stream.WriteVersion(s_UiDocumentDescVersion);

  const bool bHasRoot = m_pRoot != nullptr;
  inout_stream << bHasRoot;

  if (bHasRoot)
  {
    plReflectionSerializer::WriteObjectToBinary(inout_stream, m_pRoot->GetDynamicRTTI(), m_pRoot);
  }

  // version 2+: style reference + animation clips
  inout_stream << m_sStyle;
  inout_stream << m_Clips.GetCount();

  for (const plUiAnimationClip* pClip : m_Clips)
  {
    plReflectionSerializer::WriteObjectToBinary(inout_stream, pClip->GetDynamicRTTI(), pClip);
  }

  return PL_SUCCESS;
}

plResult plUiDocumentResourceDescriptor::Load(plStreamReader& inout_stream)
{
  Clear();

  const plTypeVersion uiVersion = inout_stream.ReadVersion(s_UiDocumentDescVersion);

  bool bHasRoot = false;
  inout_stream >> bHasRoot;

  if (bHasRoot)
  {
    const plRTTI* pRtti = nullptr;
    void* pObject = plReflectionSerializer::ReadObjectFromBinary(inout_stream, pRtti);

    if (pObject == nullptr || pRtti == nullptr || !pRtti->IsDerivedFrom<plUiElement>())
    {
      return PL_FAILURE;
    }

    m_pRoot = static_cast<plUiElement*>(pObject);
  }

  if (uiVersion >= 2)
  {
    inout_stream >> m_sStyle;

    plUInt32 uiClipCount = 0;
    inout_stream >> uiClipCount;

    for (plUInt32 i = 0; i < uiClipCount; ++i)
    {
      const plRTTI* pRtti = nullptr;
      void* pObject = plReflectionSerializer::ReadObjectFromBinary(inout_stream, pRtti);

      if (pObject == nullptr || pRtti == nullptr || !pRtti->IsDerivedFrom<plUiAnimationClip>())
      {
        return PL_FAILURE;
      }

      m_Clips.PushBack(static_cast<plUiAnimationClip*>(pObject));
    }
  }

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiDocumentResource, 1, plRTTIDefaultAllocator<plUiDocumentResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plUiDocumentResource);
// clang-format on

plUiDocumentResource::plUiDocumentResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

plUiDocumentResource::~plUiDocumentResource() = default;

plResourceLoadDesc plUiDocumentResource::UnloadData(Unload WhatToUnload)
{
  m_Descriptor.Clear();
  m_hStyle.Invalidate();

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Unloaded;

  return res;
}

plResourceLoadDesc plUiDocumentResource::UpdateContent(plStreamReader* Stream)
{
  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;

  if (Stream == nullptr)
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  // skip the absolute file path data that the standard file reader writes into the stream
  {
    plStringBuilder sAbsFilePath;
    (*Stream) >> sAbsFilePath;
  }

  plAssetFileHeader assetHeader;
  assetHeader.Read(*Stream).IgnoreResult();

  plUiDocumentResourceDescriptor desc;
  if (desc.Load(*Stream).Failed())
  {
    res.m_State = plResourceState::LoadedResourceMissing;
    return res;
  }

  return CreateResource(std::move(desc));
}

void plUiDocumentResource::UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage)
{
  out_NewMemoryUsage.m_uiMemoryCPU = sizeof(*this);
  out_NewMemoryUsage.m_uiMemoryGPU = 0;
}

PL_RESOURCE_IMPLEMENT_CREATEABLE(plUiDocumentResource, plUiDocumentResourceDescriptor)
{
  m_Descriptor = std::move(descriptor);

  m_hStyle.Invalidate();
  if (!m_Descriptor.m_sStyle.IsEmpty())
  {
    m_hStyle = plResourceManager::LoadResource<plUiStyleResource>(m_Descriptor.m_sStyle);
  }

  plResourceLoadDesc res;
  res.m_uiQualityLevelsDiscardable = 0;
  res.m_uiQualityLevelsLoadable = 0;
  res.m_State = plResourceState::Loaded;

  return res;
}

PL_STATICLINK_FILE(GameUiPlugin, GameUiPlugin_Resources_Implementation_UiDocumentResource);
