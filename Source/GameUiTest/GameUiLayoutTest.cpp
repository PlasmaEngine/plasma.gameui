#include <GameUiTest/GameUiTestPCH.h>

#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Elements/UiLayout.h>
#include <GameUiPlugin/Elements/UiTextFit.h>
#include <GameUiTest/GameUiTestFont.h>

namespace
{
  constexpr float s_fGlyph = 6.0f * 16.0f / 10.0f; ///< one glyph of the test font at 16 px

  plUiText* AddLabel(plUiElement& ref_parent, plStringView sText, const plUiFontResourceHandle& hFont, float fInset = 16.0f)
  {
    plUiText* pText = PL_DEFAULT_NEW(plUiText);
    pText->m_sText = sText;
    pText->m_hFont = hFont;
    pText->m_uiFontSize = 16;
    pText->m_HAlign = plUiTextHAlign::Center;
    pText->m_VAlign = plUiTextVAlign::Center;
    pText->m_vAnchorMin = plVec2(0.0f);
    pText->m_vAnchorMax = plVec2(1.0f);
    pText->m_vOffsets = plVec4(fInset, 0.0f, -fInset, 0.0f);
    ref_parent.m_Children.PushBack(pText);
    return pText;
  }

  plUiButton* AddButton(plUiElement& ref_parent, plStringView sName, plStringView sLabel, const plUiFontResourceHandle& hFont, float fWidth)
  {
    plUiButton* pButton = PL_DEFAULT_NEW(plUiButton);
    pButton->m_sName = sName;
    pButton->m_vOffsets = plVec4(0.0f, 0.0f, fWidth, 40.0f);
    AddLabel(*pButton, sLabel, hFont);
    ref_parent.m_Children.PushBack(pButton);
    return pButton;
  }

  void FitContent(plUiElement* pElement, plVec2 vMin, plVec2 vMax = plVec2::MakeZero())
  {
    pElement->m_SizeMode = plUiSizeMode::FitContent;
    pElement->m_vMinSize = vMin;
    pElement->m_vMaxSize = vMax;
  }

  plUiTextLayoutOptions Options(float fSize)
  {
    plUiTextLayoutOptions options;
    options.m_fFontSize = fSize;
    return options;
  }
} // namespace

PL_CREATE_SIMPLE_TEST_GROUP(GameUiLayout);

PL_CREATE_SIMPLE_TEST(GameUiLayout, TextLayout)
{
  const plUiFontResourceHandle hFont = plGameUiTestCreateFont("GameUiTestFont");
  plResourceLock<plUiFontResource> pFont(hFont, plResourceAcquireMode::BlockTillLoaded);
  PL_TEST_BOOL(pFont.GetAcquireResult() == plResourceAcquireResult::Final);

  PL_TEST_BLOCK(plTestBlock::Enabled, "Measure")
  {
    const plVec2 vSize = plUiTextLayout::Measure(pFont.GetPointer(), "Options", Options(16.0f));
    PL_TEST_FLOAT(vSize.x, 7 * s_fGlyph, 0.01f);
    PL_TEST_FLOAT(vSize.y, 10.0f * 1.6f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Letter spacing and line height")
  {
    plUiTextLayoutOptions options = Options(16.0f);
    options.m_fLetterSpacing = 2.0f;
    options.m_fLineHeightScale = 1.5f;
    const plVec2 vSize = plUiTextLayout::Measure(pFont.GetPointer(), "Back\nApply", options);
    PL_TEST_FLOAT(vSize.x, 5 * (s_fGlyph + 2.0f), 0.01f);
    PL_TEST_FLOAT(vSize.y, 2 * 16.0f * 1.5f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Wrap at spaces")
  {
    plUiTextLayoutOptions options = Options(16.0f);
    options.m_fWrapWidth = 8.5f * s_fGlyph; // "Neues " fits, "Neues Spiel" does not

    plDynamicArray<plUiTextLine> lines;
    plUiTextLayout::Layout(*pFont.GetPointer(), "Neues Spiel", options, lines);
    PL_TEST_INT(lines.GetCount(), 2);
    PL_TEST_FLOAT(lines[0].m_fWidth, 5 * s_fGlyph, 0.01f);
    PL_TEST_INT(lines[1].m_Glyphs.GetCount(), 5);
    PL_TEST_FLOAT(lines[1].m_Glyphs[0].m_fPenX, 0.0f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Missing glyphs")
  {
    // "続ける Ok"
    plHashSet<plUInt32> missing;
    plUiTextLayout::CollectMissingGlyphs(*pFont.GetPointer(), "\xE7\xB6\x9A\xE3\x81\x91\xE3\x82\x8B Ok", missing);
    PL_TEST_INT(missing.GetCount(), 3);
    PL_TEST_BOOL(missing.Contains(0x7D9A));

    // a missing character still advances half an em
    const plVec2 vSize = plUiTextLayout::Measure(pFont.GetPointer(), "\xE7\xB6\x9A", Options(16.0f));
    PL_TEST_FLOAT(vSize.x, 0.5f * 16.0f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Shrink size")
  {
    bool bFits = false;
    const float fSize = plUiTextLayout::ComputeShrinkSize(pFont.GetPointer(), "Einstellungen", Options(16.0f), false, plVec2(130.0f, 40.0f), 10.0f, &bFits);
    PL_TEST_BOOL(bFits);
    PL_TEST_FLOAT(fSize, 16.0f, 0.01f); // 13 * 0.6 * 16 = 124.8 fits 130

    const float fSmaller = plUiTextLayout::ComputeShrinkSize(pFont.GetPointer(), "Einstellungen", Options(16.0f), false, plVec2(100.0f, 40.0f), 10.0f, &bFits);
    PL_TEST_BOOL(bFits);
    PL_TEST_FLOAT(fSmaller, 12.0f, 0.01f); // 13 * 0.6 * 12 = 93.6; 13 px would need 101.4

    plUiTextLayout::ComputeShrinkSize(pFont.GetPointer(), "Einstellungen", Options(16.0f), false, plVec2(60.0f, 40.0f), 10.0f, &bFits);
    PL_TEST_BOOL(!bFits);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Pseudo-localization")
  {
    plStringBuilder sPseudo;
    plUiPseudoLocalize("Options", 0.4f, sPseudo);
    PL_TEST_STRING(sPseudo, "[\xC3\x96\xC3\xBE\xC5\xA3\xC3\xAE\xC3\xB6\xC3\xB1\xC5\xA1 ~~~]"); // [Öþţîöñš ~~~]
  }
}

PL_CREATE_SIMPLE_TEST(GameUiLayout, FitContent)
{
  const plUiFontResourceHandle hFont = plGameUiTestCreateFont("GameUiTestFont");
  plResourceLock<plUiFontResource> pFont(hFont, plResourceAcquireMode::BlockTillLoaded);

  const plRectFloat parentRect(0, 0, 800, 600);

  PL_TEST_BLOCK(plTestBlock::Enabled, "Fixed keeps the authored rect")
  {
    plUiPanel root;
    plUiButton* pButton = AddButton(root, "Btn", "Einstellungen", hFont, 170.0f);
    const plRectFloat rect = pButton->ComputeRect(parentRect);
    PL_TEST_FLOAT(rect.width, 170.0f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Button fits its label and insets")
  {
    plUiPanel root;
    plUiButton* pButton = AddButton(root, "Btn", "Einstellungen", hFont, 170.0f);
    FitContent(pButton, plVec2(120.0f, 40.0f));

    const plVec2 vFit = plUiLayout::ComputeFitSize(pButton, nullptr);
    PL_TEST_FLOAT(vFit.x, 13 * s_fGlyph + 32.0f, 0.01f);
    PL_TEST_FLOAT(vFit.y, 40.0f, 0.01f); // the minimum keeps the authored height
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Min and max clamp")
  {
    plUiPanel root;
    plUiButton* pShort = AddButton(root, "Short", "Ok", hFont, 170.0f);
    FitContent(pShort, plVec2(120.0f, 40.0f));
    PL_TEST_FLOAT(plUiLayout::ComputeFitSize(pShort, nullptr).x, 120.0f, 0.01f);

    plUiButton* pLong = AddButton(root, "Long", "Auf Standardwerte zuruecksetzen", hFont, 170.0f);
    FitContent(pLong, plVec2(120.0f, 40.0f), plVec2(240.0f, 0.0f));
    PL_TEST_FLOAT(plUiLayout::ComputeFitSize(pLong, nullptr).x, 240.0f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Free placement keeps the pivot")
  {
    plUiPanel root;

    // centred: grows both ways
    plUiButton* pCentred = AddButton(root, "Centred", "Einstellungen", hFont, 170.0f);
    pCentred->m_vOffsets = plVec4(-85.0f, -20.0f, 170.0f, 40.0f);
    FitContent(pCentred, plVec2(0.0f, 40.0f));
    const plRectFloat centred = pCentred->ComputeRect(parentRect);
    PL_TEST_FLOAT(centred.x + centred.width * 0.5f, 400.0f, 0.01f);

    // right-aligned at the right edge: grows to the left
    plUiButton* pRight = AddButton(root, "Right", "Einstellungen", hFont, 170.0f);
    pRight->m_vAnchorMin = pRight->m_vAnchorMax = plVec2(1.0f, 0.0f);
    pRight->m_vOffsets = plVec4(-190.0f, 10.0f, 170.0f, 40.0f);
    FitContent(pRight, plVec2(0.0f, 40.0f));
    const plRectFloat right = pRight->ComputeRect(parentRect);
    PL_TEST_FLOAT(right.x + right.width, 780.0f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Stack reflows")
  {
    // the footer row: right-justified, 10 px apart
    plUiStackPanel row;
    row.m_Direction = plUiStackDirection::Horizontal;
    row.m_Justification = plUiStackJustification::End;
    row.m_fSpacing = 10.0f;

    plUiButton* pBack = AddButton(row, "Back", "Zurueck", hFont, 120.0f);
    plUiButton* pApply = AddButton(row, "Apply", "Uebernehmen", hFont, 120.0f);
    FitContent(pBack, plVec2(120.0f, 40.0f));
    FitContent(pApply, plVec2(120.0f, 40.0f));

    plDynamicArray<plRectFloat> rects;
    plUiLayout::ComputeChildRects(&row, plRectFloat(0, 0, 900, 40), rects);
    PL_TEST_FLOAT(rects[1].x + rects[1].width, 900.0f, 0.01f);
    PL_TEST_FLOAT(rects[1].width, 11 * s_fGlyph + 32.0f, 0.01f);
    PL_TEST_FLOAT(rects[0].x + rects[0].width + 10.0f, rects[1].x, 0.01f);

    // a vertical menu with Start alignment: every button keeps its own width
    plUiStackPanel menu;
    menu.m_Alignment = plUiStackAlignment::Start;
    menu.m_fSpacing = 10.0f;
    plUiButton* pContinue = AddButton(menu, "Continue", "Fortsetzen", hFont, 170.0f);
    plUiButton* pOptions = AddButton(menu, "Options", "Einstellungen", hFont, 170.0f);
    FitContent(pContinue, plVec2(120.0f, 40.0f));
    FitContent(pOptions, plVec2(120.0f, 40.0f));

    plUiLayout::ComputeChildRects(&menu, plRectFloat(0, 0, 400, 400), rects);
    PL_TEST_FLOAT(rects[0].width, 10 * s_fGlyph + 32.0f, 0.01f);
    PL_TEST_FLOAT(rects[1].width, 13 * s_fGlyph + 32.0f, 0.01f);
    PL_TEST_FLOAT(rects[1].y, 50.0f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Translated texts measure")
  {
    plUiPanel root;
    plUiButton* pButton = AddButton(root, "Btn", "Menu.Options", hFont, 170.0f);
    static_cast<plUiText*>(pButton->m_Children[0])->m_bLocalized = true;
    FitContent(pButton, plVec2(0.0f, 40.0f));

    plTranslatorStorage german;
    german.StoreTranslation("Einstellungen", plHashingUtils::StringHash("Menu.Options"), plTranslationUsage::Default);

    plUiLayoutContext context;
    context.m_pTranslator = &german;
    PL_TEST_FLOAT(plUiLayout::ComputeFitSize(pButton, &context).x, 13 * s_fGlyph + 32.0f, 0.01f);

    // pseudo-localized: measured like the pseudo text itself (the test font lacks most accented letters)
    context.m_pTranslator = nullptr;
    context.m_fPseudoExpansion = 0.4f;
    plStringBuilder sPseudo;
    plUiPseudoLocalize("Menu.Options", 0.4f, sPseudo);
    const float fPseudoWidth = plUiTextLayout::Measure(pFont.GetPointer(), sPseudo, Options(16.0f)).x;
    PL_TEST_FLOAT(plUiLayout::ComputeFitSize(pButton, &context).x, fPseudoWidth + 32.0f, 0.01f);
  }
}

PL_CREATE_SIMPLE_TEST(GameUiLayout, TextFit)
{
  const plUiFontResourceHandle hFont = plGameUiTestCreateFont("GameUiTestFont");
  plResourceLock<plUiFontResource> pFont(hFont, plResourceAcquireMode::BlockTillLoaded);

  plUiPanel root;
  root.m_vAnchorMin = plVec2(0.0f);
  root.m_vAnchorMax = plVec2(1.0f);
  root.m_vOffsets = plVec4::MakeZero();

  plUiButton* pOptions = AddButton(root, "Btn_Options", "Einstellungen", hFont, 120.0f);
  static_cast<plUiText*>(pOptions->m_Children[0])->m_Overflow = plUiTextOverflow::Ellipsis;

  plUiButton* pFits = AddButton(root, "Btn_Back", "Back", hFont, 170.0f);
  PL_IGNORE_UNUSED(pFits);

  plUiButton* pJapanese = AddButton(root, "Btn_Quit", "\xE7\xB6\x9A\xE3\x81\x91\xE3\x82\x8B", hFont, 170.0f); // 続ける
  PL_IGNORE_UNUSED(pJapanese);

  plUiButton* pShrink = AddButton(root, "Btn_Reset", "Auf Standardwerte", hFont, 170.0f);
  plUiText* pShrinkText = static_cast<plUiText*>(pShrink->m_Children[0]);
  pShrinkText->m_Overflow = plUiTextOverflow::Shrink;
  pShrinkText->m_uiMinFontSize = 10;

  const plRectFloat canvas(0, 0, 960, 540);
  plDynamicArray<plUiTextFitIssue> issues;

  PL_TEST_BLOCK(plTestBlock::Enabled, "Overflows, shrinks and missing glyphs are listed")
  {
    plUiTextFit::Analyze(&root, canvas, plUiLayoutContext(), issues);
    PL_TEST_INT(issues.GetCount(), 3);

    // missing glyphs first, one issue for the font
    PL_TEST_BOOL(issues[0].m_Kind == plUiTextFitIssue::Kind::MissingGlyphs);
    PL_TEST_INT(issues[0].m_MissingGlyphs.GetCount(), 3);
    PL_TEST_INT(issues[0].m_Path.GetCount(), 2);
    PL_TEST_INT(issues[0].m_Path[0], 2);

    PL_TEST_BOOL(issues[1].m_Kind == plUiTextFitIssue::Kind::Ellipsized);
    PL_TEST_INT(issues[1].m_Path[0], 0);
    PL_TEST_FLOAT(issues[1].m_fNeeded, 13 * s_fGlyph, 0.01f);
    PL_TEST_FLOAT(issues[1].m_fAvailable, 120.0f - 32.0f, 0.01f);

    PL_TEST_BOOL(issues[2].m_Kind == plUiTextFitIssue::Kind::Shrunk);
    PL_TEST_FLOAT(issues[2].m_fShrunkSize, 13.0f, 0.01f); // 17 * 0.6 * 14 = 142.8 > 138; 13 px fits (132.6)
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Fit content clears the overflow")
  {
    FitContent(pOptions, plVec2(120.0f, 40.0f));
    plUiTextFit::Analyze(&root, canvas, plUiLayoutContext(), issues);

    bool bOptionsListed = false;
    for (const plUiTextFitIssue& issue : issues)
      bOptionsListed |= issue.m_Kind != plUiTextFitIssue::Kind::MissingGlyphs && issue.m_Path[0] == 0;

    PL_TEST_BOOL(!bOptionsListed);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Describe")
  {
    plUiTextFitIssue issue;
    issue.m_Kind = plUiTextFitIssue::Kind::Ellipsized;
    issue.m_fNeeded = 212.4f;
    issue.m_fAvailable = 170.0f;
    PL_TEST_STRING(plUiTextFit::Describe(issue, "Btn_Options"), "Btn_Options: ellipsized, needs 212 px, has 170 px");
  }
}
