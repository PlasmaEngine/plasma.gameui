#pragma once

#include <Foundation/Time/Time.h>
#include <Foundation/Time/Timestamp.h>
#include <GameUiPlugin/Resources/UiFontResource.h>
#include <EditorUi/Content.h>
#include <RendererCore/Textures/Texture2DResource.h>

class plAssetDocument;

/// \brief The preview under the UI font window's properties, engine-drawn (W5-D12): sample text in the baked font at a
/// large and a body size, the baked atlas, and its statistics (atlas size, glyph count, SDF spread, kerning pairs).
///
/// It reads the transformed output and reloads when the transform writes it again; before the first transform it
/// shows an empty state.
class plUiFontPreviewView : public plEditorUiContent
{
public:
  explicit plUiFontPreviewView(plAssetDocument* pDocument);

  /// Height the preview needs.
  static constexpr float Height = 248.0f;

protected:
  void OnUiUpdate() override;

private:
  void CheckOutputFile();
  bool LoadFont(plStringView sOutputFile);
  void BuildDocument();
  void UpdateAtlasTexture(float fWidth);

  plAssetDocument* m_pDocument = nullptr;

  plUiFontResourceHandle m_hFont;
  plString m_sSample;
  plUInt32 m_uiAtlasWidth = 0;
  plUInt32 m_uiAtlasHeight = 0;
  plDynamicArray<plUInt8> m_Atlas;
  plUInt32 m_uiGlyphs = 0;
  plUInt32 m_uiKerningPairs = 0;
  plUInt32 m_uiLoadCounter = 0;

  plTexture2DResourceHandle m_hAtlasTexture;
  float m_fAtlasTextureWidth = 0.0f;

  plTimestamp m_OutputTime;
  plTime m_LastCheck;
  plVec2 m_vBuiltSize = plVec2::MakeZero();
  bool m_bRebuild = true;
};
