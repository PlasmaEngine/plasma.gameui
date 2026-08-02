#pragma once

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

/// \brief Shared look of the GameUI editor panels, derived from the Documentation/GameUI
/// mockup tokens (see Mockup_MainWindow.html / Mockup_BindingAndStates.html :root palette).
///
/// The editor's dark QPalette has no readable "dim text" role — palette(mid) is a BORDER
/// grey (~#3c3c3c) that is illegible as text — so the mockup values are used verbatim.
namespace plUiEditorTheme
{
  // text
  constexpr const char* szTextDim = "#8e8e8e";    // --pl-text-dim
  constexpr const char* szTextBright = "#ffffff"; // --pl-text-bright
  constexpr const char* szText = "#cccccc";       // --pl-text

  // surfaces / borders
  constexpr const char* szWindow = "#1e1e1e";      // --pl-window
  constexpr const char* szPanelAlt = "#2d2d2d";    // --pl-alt
  constexpr const char* szBorder = "#3c3c3c";      // --pl-mid
  constexpr const char* szHeaderStrip = "#262626"; // mockup .phead background

  // accents
  constexpr const char* szAccentUi = "#b996cd";   // --pl-ui (UI asset category, PastelLavender)
  constexpr const char* szAccentAnim = "#845ef7"; // --wk-anim (violet)
  constexpr const char* szAccentBind = "#22b8cf"; // --wk-bind (cyan)
  constexpr const char* szBrand = "#0bdeb3";      // --pl-brand (teal)
  constexpr const char* szBrandBright = "#06fac9";
  constexpr const char* szQssAccent = "#085242"; // Plasma.qss selection accent
  constexpr const char* szWarning = "#fcc419";   // --wk-text (yellow)

  // ready-made style sheets
  constexpr const char* szSectionHeader = "font-size: 10px; letter-spacing: 1px; font-weight: 600; color: #b996cd;";
  constexpr const char* szHintText = "color: #8e8e8e; font-size: 9px;";
  constexpr const char* szMonoBand = "color: #8e8e8e; font-size: 9px; font-family: Consolas, monospace; background: #2d2d2d; border: 1px solid #3c3c3c; border-radius: 4px; padding: 6px 10px;";

  // mockup-styled item views (Plasma.qss-accent selection, header strip sections)
  constexpr const char* szTableStyle =
    "QTableWidget { background: #1e1e1e; alternate-background-color: #232323; gridline-color: #2d2d2d; border: none; }"
    "QTableWidget::item { padding: 2px 4px; }"
    "QTableWidget::item:selected { background: #085242; color: #ffffff; }"
    "QHeaderView::section { background: #262626; color: #8e8e8e; border: none; border-bottom: 1px solid #3c3c3c; border-right: 1px solid #2d2d2d; padding: 3px 6px; font-size: 10px; letter-spacing: 1px; }";

  constexpr const char* szListStyle =
    "QListWidget { background: #1e1e1e; border: none; }"
    "QListWidget::item { padding: 4px 6px; border-radius: 2px; }"
    "QListWidget::item:hover { background: #2d2d2d; }"
    "QListWidget::item:selected { background: #085242; color: #ffffff; }";

  inline QColor GetDimTextColor() { return QColor(0x8e, 0x8e, 0x8e); }

  /// \brief A mockup-style bordered panel with a dot + title header strip (.panel/.phead).
  ///
  /// Returns the frame; out_pBodyLayout is where the content goes. Optional access to the
  /// header row (to place controls inside the strip) and the title label (dynamic titles).
  inline QFrame* CreatePanel(QWidget* pParent, const char* szDotColor, const QString& sTitle, QVBoxLayout*& out_pBodyLayout, QHBoxLayout** out_pHeaderLayout = nullptr, QLabel** out_pTitleLabel = nullptr)
  {
    QFrame* pFrame = new QFrame(pParent);
    pFrame->setObjectName("plUiMockupPanel");
    pFrame->setStyleSheet("#plUiMockupPanel { background: #1e1e1e; border: 1px solid #3c3c3c; border-radius: 4px; }");

    QVBoxLayout* pFrameLayout = new QVBoxLayout(pFrame);
    pFrameLayout->setContentsMargins(1, 1, 1, 1);
    pFrameLayout->setSpacing(0);

    QWidget* pHeader = new QWidget(pFrame);
    pHeader->setObjectName("plUiMockupPanelHead");
    pHeader->setStyleSheet("#plUiMockupPanelHead { background: #262626; border-bottom: 1px solid #3c3c3c; }");

    QHBoxLayout* pHeaderLayout = new QHBoxLayout(pHeader);
    pHeaderLayout->setContentsMargins(10, 5, 10, 5);
    pHeaderLayout->setSpacing(8);

    QLabel* pDot = new QLabel(pHeader);
    pDot->setFixedSize(8, 8);
    pDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(szDotColor));
    pHeaderLayout->addWidget(pDot);

    QLabel* pTitle = new QLabel(sTitle, pHeader);
    pTitle->setStyleSheet("font-size: 11px; font-weight: 600; color: #ffffff; background: transparent;");
    pHeaderLayout->addWidget(pTitle);
    pHeaderLayout->addStretch();

    pFrameLayout->addWidget(pHeader);

    QWidget* pBody = new QWidget(pFrame);
    pBody->setStyleSheet("background: transparent;");

    out_pBodyLayout = new QVBoxLayout(pBody);
    out_pBodyLayout->setContentsMargins(8, 8, 8, 8);
    out_pBodyLayout->setSpacing(6);

    pFrameLayout->addWidget(pBody, 1);

    if (out_pHeaderLayout != nullptr)
    {
      *out_pHeaderLayout = pHeaderLayout;
    }

    if (out_pTitleLabel != nullptr)
    {
      *out_pTitleLabel = pTitle;
    }

    return pFrame;
  }
} // namespace plUiEditorTheme
