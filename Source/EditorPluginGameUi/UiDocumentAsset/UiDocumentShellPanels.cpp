#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/EngineShell/AssetDocumentContent.h>
#include <EditorFramework/EngineShell/DocumentFrame.h>
#include <EditorFramework/EngineShell/EngineViewPanel.h>
#include <EditorFramework/EngineShell/WindowContent.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiCanvasViews.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAsset.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentAssetWindow.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiDocumentShellPanels.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEngineWidgets.h>
#include <EditorUi/Widgets.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.h>

namespace
{
  /// Lends the window's canvas chrome (the rulers and the status row) to the shell's view panel as edge children, and
  /// takes it back when the window closes. The link is a hidden child of the view panel, so it goes with the panel.
  class plUiCanvasChromeLink : public plEditorUiContent
  {
  public:
    plUiCanvasChromeLink(plEditorUiEngineViewPanel& ref_viewPanel, const plUiDocumentAssetDocumentWindow& window)
      : plEditorUiContent("GameUi.CanvasChromeLink")
      , m_pViewPanel(&ref_viewPanel)
      , m_pWindow(&window)
    {
      using Edge = plEditorUiEngineViewPanel::Edge;

      Lend(window.GetHorizontalRuler(), Edge::Top, plUiRulerView::Thickness);
      Lend(window.GetVerticalRuler(), Edge::Left, plUiRulerView::Thickness);
      Lend(window.GetCanvasStatusRow(), Edge::Bottom, plUiCanvasStatusRow::GetHeight());

      plDocumentWindow::s_Events.AddEventHandler(plMakeDelegate(&plUiCanvasChromeLink::DocumentWindowEventHandler, this));
    }

    ~plUiCanvasChromeLink()
    {
      plDocumentWindow::s_Events.RemoveEventHandler(plMakeDelegate(&plUiCanvasChromeLink::DocumentWindowEventHandler, this));
    }

  private:
    void Lend(plEditorUiContent* pChild, plEditorUiEngineViewPanel::Edge edge, float fThickness)
    {
      if (pChild == nullptr || !pChild->IsReady())
        return;

      m_pViewPanel->AddLentChild(*pChild);
      m_pViewPanel->SetEdgeChild(*pChild, edge, fThickness);
      m_Lent.PushBack(pChild);
    }

    /// The window deletes the chrome after this event: the view panel gives it back first.
    void DocumentWindowEventHandler(const plDocumentWindowEvent& e)
    {
      if (e.m_Type != plDocumentWindowEvent::WindowClosing || e.m_pWindow != m_pWindow)
        return;

      for (const plEditorUiContent* pChild : m_Lent)
        m_pViewPanel->RemoveChild(pChild);

      m_Lent.Clear();
      m_pWindow = nullptr;
    }

    plEditorUiEngineViewPanel* m_pViewPanel = nullptr;
    const plDocumentWindow* m_pWindow = nullptr;
    plHybridArray<const plEditorUiContent*, 3> m_Lent;
  };

  /// The canvas's rulers and status row along the edges of the shell's view.
  void AddCanvasChrome(plEditorUiDocumentFrame& ref_frame)
  {
    const auto* pWindow = dynamic_cast<const plUiDocumentAssetDocumentWindow*>(ref_frame.GetContextWindow());
    plEditorUiSurface* pSurface = ref_frame.GetPanels().GetPanelSurface("View");
    auto* pViewPanel = pSurface != nullptr ? dynamic_cast<plEditorUiEngineViewPanel*>(plEditorUiContent::Find(pSurface)) : nullptr;
    if (pWindow == nullptr || pViewPanel == nullptr)
      return;

    pViewPanel->AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plUiCanvasChromeLink, *pViewPanel, *pWindow)));
  }

  void AddUiDocumentPanels(plEditorUiDocumentFrame& ref_frame)
  {
    using plEditorUi::Icon;

    AddCanvasChrome(ref_frame);

    // the window declares the panels; they get the package's icons
    plEditorUiDockManager& panels = ref_frame.GetPanels();
    panels.SetPanelIcon("Palette", plGameUiEditor::IconPath("Palette"));
    panels.SetPanelIcon("Hierarchy", plEditorUi::IconPath(Icon::Tree));
    panels.SetPanelIcon("RectTransform", plGameUiEditor::IconPath("Anchor"));
    panels.SetPanelIcon("Style", plGameUiEditor::IconPath("Style"));
    panels.SetPanelIcon("Bindings", plGameUiEditor::IconPath("Link"));
    panels.SetPanelIcon("States", plEditorUi::IconPath(Icon::Layers));
    panels.SetPanelIcon("Animations", plEditorUi::IconPath(Icon::AnimationClip));
    panels.SetPanelIcon("SizeFit", plGameUiEditor::IconPath("FitContent"));
    panels.SetPanelIcon("TextFit", plGameUiEditor::IconPath("TextFit"));

    // the canvas leads; lists to pick from on the left, what the selection is on the right, the data below
    panels.DockPanel("Palette", "View", plEditorUiDockSide::Left, 0.2f);
    panels.DockPanel("Hierarchy", "Palette", plEditorUiDockSide::Bottom, 0.6f);
    panels.DockPanel("RectTransform", "Properties", plEditorUiDockSide::Top, 0.3f);
    panels.DockPanel("SizeFit", "Properties", plEditorUiDockSide::Top, 0.22f); // between the rect transform and the properties
    panels.DockPanel("TextFit", "Properties", plEditorUiDockSide::Bottom, 0.3f);
    panels.DockPanel("Style", "Properties", plEditorUiDockSide::Tab);
    panels.DockPanel("Bindings", "View", plEditorUiDockSide::Bottom, 0.3f);
    panels.DockPanel("States", "Bindings", plEditorUiDockSide::Tab);
    panels.DockPanel("Animations", "Bindings", plEditorUiDockSide::Tab);
    panels.SetCurrentPanel("Properties");
    panels.SetCurrentPanel("Bindings");
  }

  plEngineAssetPanelsProvider s_UiDocumentPanelsProvider(plGetStaticRTTI<plUiDocumentAssetDocument>(), &AddUiDocumentPanels);

  /// The font window's preview (plUiFontAssetDocumentWindow) goes below its properties.
  void ArrangeFont(plEditorUiDockManager& ref_panels)
  {
    ref_panels.SetPanelIcon("FontPreview", plGameUiEditor::IconPath("Font"));
    ref_panels.DockPanel("FontPreview", "Properties", plEditorUiDockSide::Bottom, 0.45f);
  }
} // namespace

void plUiDocumentShellPanels::Register()
{
  plEditorUiDocumentContentProvider::Register(&s_UiDocumentPanelsProvider);
  plWindowContentProvider::SetDefaultArrangement("UI Font", &ArrangeFont);
}

void plUiDocumentShellPanels::Unregister()
{
  plWindowContentProvider::SetDefaultArrangement("UI Font", nullptr);
  plEditorUiDocumentContentProvider::Unregister(&s_UiDocumentPanelsProvider);
}
