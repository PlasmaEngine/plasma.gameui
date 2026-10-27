#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/Assets/AssetDocument.h>
#include <EditorFramework/Assets/AssetDocumentManager.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorPluginGameUi/UiFontAsset/UiFontPreviewView.h>
#include <EditorUi/Surface.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <Foundation/Utilities/ConversionUtils.h>

namespace
{
  constexpr float s_fInset = 10.0f;
  constexpr float s_fAtlasHeight = 92.0f;

  /// The SDF spread the font transform asks FreeType for (UiFontAsset.cpp).
  constexpr plUInt32 s_uiSdfSpread = 16;

  /// Sample characters: Latin, Latin Extended, digits and symbols. Characters the font did not bake are left out.
  constexpr const char* s_szSample = "Aa Bb \xC4\x86\xC4\x87 \xC5\xBB\xC5\xBC 0123 \xE2\x86\x90\xE2\x98\x85";
} // namespace

plUiFontPreviewView::plUiFontPreviewView(plAssetDocument* pDocument)
  : plEditorUiContent("GameUi.FontPreview")
  , m_pDocument(pDocument)
{
}

void plUiFontPreviewView::OnUiUpdate()
{
  if (!IsReady())
    return;

  // the transform writes the output while the window is open: look for a newer one twice a second
  if (plTime::Now() - m_LastCheck > plTime::MakeFromMilliseconds(500))
  {
    m_LastCheck = plTime::Now();
    CheckOutputFile();
  }

  if (m_bRebuild || GetSurface()->GetLogicalSize() != m_vBuiltSize)
    BuildDocument();
}

void plUiFontPreviewView::CheckOutputFile()
{
  const plString sOutput = m_pDocument->GetAssetDocumentManager()->GetAbsoluteOutputFileName(m_pDocument->GetAssetDocumentTypeDescriptor(), m_pDocument->GetDocumentPath(), "");

  plFileStats stats;
  if (plOSFile::GetFileStats(sOutput, stats).Failed())
  {
    if (m_hFont.IsValid())
    {
      m_hFont.Invalidate();
      m_OutputTime = plTimestamp();
      m_bRebuild = true;
    }
    return;
  }

  if (m_hFont.IsValid() && stats.m_LastModificationTime.Compare(m_OutputTime, plTimestamp::CompareMode::Identical))
    return;

  m_OutputTime = stats.m_LastModificationTime;
  if (!LoadFont(sOutput))
    m_hFont.Invalidate();

  m_bRebuild = true;
}

bool plUiFontPreviewView::LoadFont(plStringView sOutputFile)
{
  plFileReader file;
  if (file.Open(sOutputFile).Failed())
    return false;

  plAssetFileHeader header;
  if (header.Read(file).Failed())
    return false;

  plUiFontResourceDescriptor desc;
  if (desc.Load(file).Failed() || desc.m_Glyphs.IsEmpty())
    return false;

  m_uiAtlasWidth = desc.m_uiAtlasWidth;
  m_uiAtlasHeight = desc.m_uiAtlasHeight;
  m_Atlas = desc.m_AtlasData;
  m_uiGlyphs = desc.m_Glyphs.GetCount();
  m_uiKerningPairs = desc.m_Kerning.GetCount();

  // the sample keeps the characters the font has
  plHashSet<plUInt32> baked;
  for (const plUiFontGlyph& glyph : desc.m_Glyphs)
    baked.Insert(glyph.m_uiCodepoint);

  plStringBuilder sSample;
  for (auto it = plStringView(s_szSample).GetIteratorFront(); it.IsValid(); ++it)
  {
    const plUInt32 uiChar = it.GetCharacter();
    if (uiChar == ' ' ? !sSample.IsEmpty() && !sSample.EndsWith(" ") : baked.Contains(uiChar))
      sSample.Append(uiChar);
  }

  sSample.Trim(" ");
  m_sSample = sSample;

  // a new resource per load: text drawn with the previous one may still be in flight
  plStringBuilder sId;
  plStringBuilder sGuid;
  sId.SetFormat("GameUiFontPreview/{}/{}", plConversionUtils::ToString(m_pDocument->GetGuid(), sGuid), ++m_uiLoadCounter);
  m_hFont = plResourceManager::CreateResource<plUiFontResource>(sId, std::move(desc));

  m_fAtlasTextureWidth = 0.0f;
  return m_hFont.IsValid();
}

void plUiFontPreviewView::UpdateAtlasTexture(float fWidth)
{
  if (m_Atlas.IsEmpty() || m_uiAtlasWidth == 0 || m_uiAtlasHeight == 0)
  {
    m_hAtlasTexture.Invalidate();
    return;
  }

  if (m_fAtlasTextureWidth == fWidth && m_hAtlasTexture.IsValid())
    return;

  m_fAtlasTextureWidth = fWidth;

  // the strip shows the atlas's top rows at the strip's aspect, so the glyphs keep their shape
  const plUInt32 uiStep = plMath::Max<plUInt32>(1, (m_uiAtlasWidth + 1023) / 1024);
  const plUInt32 uiRows = plMath::Min(m_uiAtlasHeight, static_cast<plUInt32>(plMath::Ceil(m_uiAtlasWidth * (s_fAtlasHeight - 2.0f) / plMath::Max(fWidth, 16.0f))));
  const plUInt32 uiWidth = m_uiAtlasWidth / uiStep;
  const plUInt32 uiHeight = plMath::Max<plUInt32>(1, uiRows / uiStep);

  const plColorGammaUB background(11, 11, 13);
  const plColorGammaUB glyph(236, 236, 239);

  plDynamicArray<plColorGammaUB> pixels;
  pixels.SetCountUninitialized(uiWidth * uiHeight);

  for (plUInt32 y = 0; y < uiHeight; ++y)
  {
    for (plUInt32 x = 0; x < uiWidth; ++x)
    {
      // the SDF edge is at 128: a soft step around it draws the glyph shapes
      const float fDistance = m_Atlas[(y * uiStep) * m_uiAtlasWidth + x * uiStep];
      const float fCoverage = plMath::Clamp((fDistance - 104.0f) / 48.0f, 0.0f, 1.0f);
      pixels[y * uiWidth + x] = plGameUiEditor::Mix(background, glyph, fCoverage);
    }
  }

  plStringBuilder sGuid;
  plStringBuilder sName;
  sName.SetFormat("GameUiFontAtlas/{}/{}/{}", plConversionUtils::ToString(m_pDocument->GetGuid(), sGuid), m_uiLoadCounter, static_cast<plUInt32>(fWidth));

  m_hAtlasTexture = plEditorUi::ImageTexture(sName, uiWidth, uiHeight, pixels);
}

void plUiFontPreviewView::BuildDocument()
{
  using namespace plEditorUi;

  m_bRebuild = false;
  m_vBuiltSize = GetSurface()->GetLogicalSize();

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  auto* pLine = Panel(pRoot);
  pLine->m_vAnchorMax.y = 0;
  pLine->m_vOffsets = plVec4(0, 0, 0, 1);
  pLine->m_Color = Line();

  plUiPanel* pHeader = plGameUiEditor::SectionHeader(pRoot, plTranslate("GameUi.FontPreview.Title"), {}, plGameUiEditor::UiColor());
  pHeader->m_vOffsets.y = 1.0f;

  const float fTop = SectionHeaderHeight + 1.0f;

  if (!m_hFont.IsValid())
  {
    plUiPanel* pEmpty = plGameUiEditor::EmptyState(pRoot, plGameUiEditor::IconPath("Font"), plTranslate("GameUi.FontPreview.Empty"), plTranslate("GameUi.FontPreview.EmptyLine"));
    Fill(pEmpty, plVec4(0, fTop, 0, 0));
    SetDocument(Finish(pRoot));
    return;
  }

  const float fWidth = plMath::Max(m_vBuiltSize.x - 2.0f * s_fInset, 16.0f);
  float y = fTop + 10.0f;

  // the sample at a large and at the body size, in the baked font
  auto* pLarge = Text(pRoot, m_sSample);
  plGameUiEditor::Place(pLarge, plRectFloat(s_fInset, y, fWidth, 36.0f));
  pLarge->m_hFont = m_hFont;
  pLarge->m_uiFontSize = 28;
  pLarge->m_Color = Foreground();
  y += 40.0f;

  auto* pBody = Text(pRoot, plTranslate("GameUi.FontPreview.Pangram"));
  plGameUiEditor::Place(pBody, plRectFloat(s_fInset, y, fWidth, 20.0f));
  pBody->m_hFont = m_hFont;
  pBody->m_uiFontSize = 14;
  pBody->m_Color = Muted();
  y += 26.0f;

  // the baked atlas
  auto* pFrame = Panel(pRoot);
  plGameUiEditor::Place(pFrame, plRectFloat(s_fInset, y, fWidth, s_fAtlasHeight));
  pFrame->m_Color = plColorGammaUB(11, 11, 13);
  pFrame->m_fBorderWidth = 1.0f;
  pFrame->m_BorderColor = Line();
  pFrame->m_vCornerRadius = plVec4(4);

  UpdateAtlasTexture(fWidth - 2.0f);
  if (m_hAtlasTexture.IsValid())
  {
    plUiImage* pAtlas = PL_DEFAULT_NEW(plUiImage);
    pAtlas->m_BrushMode = plUiBrushMode::Image;
    pAtlas->m_hTexture = m_hAtlasTexture;
    pAtlas->m_Color = plColorGammaUB(255, 255, 255, 255);
    Fill(pAtlas, plVec4(1, 1, 1, 1));
    pFrame->m_Children.PushBack(pAtlas);
  }

  y += s_fAtlasHeight + 6.0f;

  plStringBuilder sStats;
  sStats.SetFormat("atlas {} \xC3\x97 {} \xC2\xB7 {} glyphs \xC2\xB7 SDF spread {} \xC2\xB7 {} kerning pairs", m_uiAtlasWidth, m_uiAtlasHeight, m_uiGlyphs, s_uiSdfSpread, m_uiKerningPairs);

  auto* pStats = Text(pRoot, sStats);
  plGameUiEditor::Place(pStats, plRectFloat(s_fInset, y, fWidth, 18.0f));
  pStats->m_hFont = GetCodeFont();
  pStats->m_uiFontSize = FontSize - 2;
  pStats->m_Color = Dim();

  SetDocument(Finish(pRoot));
}
