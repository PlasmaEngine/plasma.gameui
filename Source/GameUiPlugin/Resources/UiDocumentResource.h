#pragma once

#include <Core/ResourceManager/Resource.h>
#include <GameUiPlugin/Elements/UiAnimationClip.h>
#include <GameUiPlugin/Elements/UiElements.h>
#include <GameUiPlugin/GameUiPluginDLL.h>
#include <GameUiPlugin/Resources/UiStyleResource.h>

/// \brief Compiled data of a UI Document asset: the widget tree, its animation clips and
/// its UI Style reference.
struct PL_GAMEUIPLUGIN_DLL plUiDocumentResourceDescriptor
{
  plUiDocumentResourceDescriptor();
  plUiDocumentResourceDescriptor(plUiDocumentResourceDescriptor&& other);
  ~plUiDocumentResourceDescriptor();

  void operator=(plUiDocumentResourceDescriptor&& rhs);

  plResult Save(plStreamWriter& inout_stream);
  plResult Load(plStreamReader& inout_stream);

  void Clear();

  /// The root element. Owned by the descriptor.
  plUiElement* m_pRoot = nullptr;

  /// Named keyframe animation clips. Owned by the descriptor.
  plDynamicArray<plUiAnimationClip*> m_Clips;

  /// Resource id of the document's UI Style; empty = unstyled. Unlike the design-time
  /// BlackboardTemplate this IS compiled and used at runtime — the resource resolves it
  /// to a handle on load (see plUiDocumentResource::GetStyle).
  plString m_sStyle;
};

using plUiDocumentResourceHandle = plTypedResourceHandle<class plUiDocumentResource>;

/// \brief Resource wrapping a compiled UI Document (widget tree).
class PL_GAMEUIPLUGIN_DLL plUiDocumentResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plUiDocumentResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plUiDocumentResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plUiDocumentResource, plUiDocumentResourceDescriptor);

public:
  plUiDocumentResource();
  ~plUiDocumentResource();

  /// \brief The root element of the widget tree. May be nullptr for an empty document.
  const plUiElement* GetRoot() const { return m_Descriptor.m_pRoot; }

  /// \brief The document's animation clips.
  const plDynamicArray<plUiAnimationClip*>& GetClips() const { return m_Descriptor.m_Clips; }

  /// \brief The document's UI Style; invalid handle = unstyled. The handle pins the style
  /// for the document's lifetime (same rule as the tree's texture handles).
  const plUiStyleResourceHandle& GetStyle() const { return m_hStyle; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* Stream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  plUiDocumentResourceDescriptor m_Descriptor;
  plUiStyleResourceHandle m_hStyle;
};
