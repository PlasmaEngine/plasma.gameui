#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiDocumentAsset/UiAnimationsPanel.moc.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiEditorTheme.h>
#include <Foundation/Math/Math.h>
#include <Foundation/Strings/StringBuilder.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <GameUiPlugin/Elements/UiAnimationClip.h>

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
  constexpr int s_iGutterWidth = 180;
  constexpr int s_iRulerHeight = 22;
  constexpr int s_iRowHeight = 26;
  constexpr int s_iRightPad = 14;
  constexpr int s_iKeyHalf = 5;

  QColor GetTargetColor(plInt32 iTarget)
  {
    // widget-kind palette from Documentation/GameUI (plColorScheme::LightUI shades)
    switch (iTarget)
    {
      case plUiAnimTarget::OffsetX:
      case plUiAnimTarget::OffsetY:
        return QColor(0x33, 0x9A, 0xF0); // blue: placement
      case plUiAnimTarget::Width:
      case plUiAnimTarget::Height:
        return QColor(0x22, 0xB8, 0xCF); // cyan: size
      case plUiAnimTarget::Opacity:
        return QColor(0x94, 0xD8, 0x2D); // lime: visibility
      case plUiAnimTarget::Scale:
        return QColor(0xFC, 0xC4, 0x19); // yellow: scale
      case plUiAnimTarget::Rotation:
        return QColor(0xE6, 0x49, 0x80); // pink: rotation
      case plUiAnimTarget::Fill:
        return QColor(0xFF, 0x92, 0x2B); // orange: controls
      case plUiAnimTarget::Tint:
      default:
        return QColor(0x84, 0x5E, 0xF7); // violet: color
    }
  }

  const char* GetTargetName(plInt32 iTarget)
  {
    switch (iTarget)
    {
      case plUiAnimTarget::OffsetX:
        return "Offset.X";
      case plUiAnimTarget::OffsetY:
        return "Offset.Y";
      case plUiAnimTarget::Opacity:
        return "Opacity";
      case plUiAnimTarget::Scale:
        return "Scale";
      case plUiAnimTarget::Width:
        return "Width";
      case plUiAnimTarget::Height:
        return "Height";
      case plUiAnimTarget::Rotation:
        return "Rotation";
      case plUiAnimTarget::Fill:
        return "Fill";
      case plUiAnimTarget::Tint:
      default:
        return "Tint";
    }
  }

  float ApplyEasingEditor(plInt32 iEasing, float fT)
  {
    // mirrors the runtime plUiAnimEasing evaluation, for the lane curve preview
    switch (iEasing)
    {
      case plUiAnimEasing::EaseIn:
        return fT * fT;
      case plUiAnimEasing::EaseOut:
        return 1.0f - (1.0f - fT) * (1.0f - fT);
      case plUiAnimEasing::EaseInOut:
        return fT < 0.5f ? 2.0f * fT * fT : 1.0f - 2.0f * (1.0f - fT) * (1.0f - fT);
      default:
        return fT;
    }
  }

  // ————— vector-pair rows: X/Y component tracks of one element share a single lane —————

  plInt32 GetPairTarget(plInt32 iTarget)
  {
    switch (iTarget)
    {
      case plUiAnimTarget::OffsetX:
        return plUiAnimTarget::OffsetY;
      case plUiAnimTarget::OffsetY:
        return plUiAnimTarget::OffsetX;
      case plUiAnimTarget::Width:
        return plUiAnimTarget::Height;
      case plUiAnimTarget::Height:
        return plUiAnimTarget::Width;
      default:
        return -1;
    }
  }

  bool IsPairPrimary(plInt32 iTarget)
  {
    return iTarget == plUiAnimTarget::OffsetX || iTarget == plUiAnimTarget::Width;
  }

  const char* GetPairName(plInt32 iTarget)
  {
    return (iTarget == plUiAnimTarget::OffsetX || iTarget == plUiAnimTarget::OffsetY) ? "Offset" : "Size";
  }

  QColor GetAxisColor(plInt32 iAxis)
  {
    // the X/Y accent colors of the vector spinboxes (plColorScheme::LightUI red/green)
    return iAxis == 0 ? QColor(0xF0, 0x69, 0x69) : QColor(0x69, 0xDB, 0x7C);
  }

  struct TimelineRow
  {
    plInt32 m_iTracks[2] = {-1, -1}; ///< track indices; [1] >= 0 = merged vector row

    bool IsMerged() const { return m_iTracks[1] >= 0; }
  };

  void BuildTimelineRows(const plQtUiAnimationsPanel::ClipInfo* pClip, plHybridArray<TimelineRow, 16>& out_rows)
  {
    out_rows.Clear();

    if (pClip == nullptr)
      return;

    plHybridArray<bool, 16> consumed;
    consumed.SetCount(pClip->m_Tracks.GetCount(), false);

    for (plUInt32 i = 0; i < pClip->m_Tracks.GetCount(); ++i)
    {
      if (consumed[i])
        continue;

      consumed[i] = true;

      TimelineRow& row = out_rows.ExpandAndGetRef();
      row.m_iTracks[0] = static_cast<plInt32>(i);

      const plInt32 iPairTarget = GetPairTarget(pClip->m_Tracks[i].m_iTarget);

      if (iPairTarget >= 0)
      {
        for (plUInt32 j = 0; j < pClip->m_Tracks.GetCount(); ++j)
        {
          if (!consumed[j] && pClip->m_Tracks[j].m_iTarget == iPairTarget && pClip->m_Tracks[j].m_sElement == pClip->m_Tracks[i].m_sElement)
          {
            consumed[j] = true;

            // [0] is always the X/Width component
            if (IsPairPrimary(pClip->m_Tracks[i].m_iTarget))
            {
              row.m_iTracks[1] = static_cast<plInt32>(j);
            }
            else
            {
              row.m_iTracks[1] = row.m_iTracks[0];
              row.m_iTracks[0] = static_cast<plInt32>(j);
            }

            break;
          }
        }
      }
    }
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// plQtUiTimelineWidget

plQtUiTimelineWidget::plQtUiTimelineWidget(plQtUiAnimationsPanel* pPanel)
  : QWidget(pPanel)
  , m_pPanel(pPanel)
{
  setMouseTracking(true);
  setFocusPolicy(Qt::ClickFocus);
}

QSize plQtUiTimelineWidget::minimumSizeHint() const
{
  plHybridArray<TimelineRow, 16> rows;
  BuildTimelineRows(m_pPanel->GetSelectedClip(), rows);

  const int iRows = plMath::Max<int>(1, static_cast<int>(rows.GetCount()));
  return QSize(420, s_iRulerHeight + iRows * s_iRowHeight + 4);
}

float plQtUiTimelineWidget::TimeToX(float fTime) const
{
  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();
  const float fDuration = (pClip != nullptr) ? plMath::Max(pClip->m_fDuration, 0.0001f) : 1.0f;
  const float fLaneWidth = static_cast<float>(width() - s_iGutterWidth - s_iRightPad);
  return s_iGutterWidth + (fTime / fDuration) * plMath::Max(fLaneWidth, 1.0f);
}

float plQtUiTimelineWidget::XToTime(float fX) const
{
  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();
  const float fDuration = (pClip != nullptr) ? plMath::Max(pClip->m_fDuration, 0.0001f) : 1.0f;
  const float fLaneWidth = plMath::Max(static_cast<float>(width() - s_iGutterWidth - s_iRightPad), 1.0f);
  return plMath::Clamp((fX - s_iGutterWidth) / fLaneWidth, 0.0f, 1.0f) * fDuration;
}

int plQtUiTimelineWidget::TrackAtY(int iY) const
{
  // returns the ROW index (a row shows 1 track, or an X/Y vector pair)
  plHybridArray<TimelineRow, 16> rows;
  BuildTimelineRows(m_pPanel->GetSelectedClip(), rows);

  if (rows.IsEmpty() || iY < s_iRulerHeight)
    return -1;

  const int iRow = (iY - s_iRulerHeight) / s_iRowHeight;
  return (iRow >= 0 && iRow < static_cast<int>(rows.GetCount())) ? iRow : -1;
}

bool plQtUiTimelineWidget::HitTestKey(const QPoint& pos, plUInt32& out_uiTrack, plUInt32& out_uiKey) const
{
  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();
  if (pClip == nullptr)
    return false;

  plHybridArray<TimelineRow, 16> rows;
  BuildTimelineRows(pClip, rows);

  for (plUInt32 uiRow = 0; uiRow < rows.GetCount(); ++uiRow)
  {
    // keys sit at their value height on float lanes, so accept the whole row vertically
    const int iTop = s_iRulerHeight + static_cast<int>(uiRow) * s_iRowHeight;

    if (pos.y() < iTop || pos.y() >= iTop + s_iRowHeight)
      continue;

    for (plInt32 iTrackInRow = 0; iTrackInRow < 2; ++iTrackInRow)
    {
      const plInt32 iTrack = rows[uiRow].m_iTracks[iTrackInRow];

      if (iTrack < 0)
        continue;

      for (plUInt32 uiKey = 0; uiKey < pClip->m_Tracks[iTrack].m_Keys.GetCount(); ++uiKey)
      {
        const int iX = static_cast<int>(TimeToX(pClip->m_Tracks[iTrack].m_Keys[uiKey].m_fTime));

        if (plMath::Abs(pos.x() - iX) <= s_iKeyHalf + 2)
        {
          out_uiTrack = static_cast<plUInt32>(iTrack);
          out_uiKey = uiKey;
          return true;
        }
      }
    }
  }

  return false;
}

void plQtUiTimelineWidget::paintEvent(QPaintEvent* pEvent)
{
  QPainter p(this);
  p.fillRect(rect(), QColor(0x1e, 0x1e, 0x1e)); // --pl-window

  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

  if (pClip == nullptr)
  {
    p.setPen(plUiEditorTheme::GetDimTextColor());
    p.drawText(rect(), Qt::AlignCenter, plMakeQString(plTranslate("GameUi.Animations.NoClip")));
    return;
  }

  const float fDuration = plMath::Max(pClip->m_fDuration, 0.0001f);

  // ————— time ruler —————
  {
    p.fillRect(QRect(0, 0, width(), s_iRulerHeight), QColor(0x19, 0x19, 0x19)); // mockup .ticksbar
    p.fillRect(QRect(0, 0, s_iGutterWidth, s_iRulerHeight), QColor(0x2d, 0x2d, 0x2d));
    p.setPen(QColor(0x3c, 0x3c, 0x3c));
    p.drawLine(0, s_iRulerHeight - 1, width(), s_iRulerHeight - 1);
    p.drawLine(s_iGutterWidth - 1, 0, s_iGutterWidth - 1, height());
    p.setPen(plUiEditorTheme::GetDimTextColor());

    // pick a step that keeps labels readable
    float fStep = 0.1f;
    while (TimeToX(fStep) - TimeToX(0.0f) < 44.0f)
    {
      fStep *= 2.0f;
    }

    QFont font = p.font();
    font.setPointSizeF(7.5);
    p.setFont(font);

    for (float t = 0.0f; t <= fDuration + fStep * 0.5f; t += fStep)
    {
      const float fT = plMath::Min(t, fDuration);
      const int iX = static_cast<int>(TimeToX(fT));
      p.drawLine(iX, s_iRulerHeight - 5, iX, s_iRulerHeight);
      p.drawText(QPoint(iX + 3, s_iRulerHeight - 7), QString::number(fT, 'f', 1));

      if (fT >= fDuration)
        break;
    }
  }

  // ————— rows (one track, or an X/Y vector pair sharing a lane) —————
  plHybridArray<TimelineRow, 16> rows;
  BuildTimelineRows(pClip, rows);

  for (plUInt32 uiRow = 0; uiRow < rows.GetCount(); ++uiRow)
  {
    const TimelineRow& row = rows[uiRow];
    const plQtUiAnimationsPanel::TrackInfo& primary = pClip->m_Tracks[row.m_iTracks[0]];

    const int iTop = s_iRulerHeight + static_cast<int>(uiRow) * s_iRowHeight;
    const int iCenterY = iTop + s_iRowHeight / 2;

    bool bSelected = false;
    for (plInt32 t = 0; t < 2; ++t)
    {
      if (row.m_iTracks[t] >= 0 && m_pPanel->IsTrackSelected(pClip->m_Tracks[row.m_iTracks[t]].m_Guid))
        bSelected = true;
    }

    // gutter (mockup .tlrow .nm: alt surface, kind square, border-right)
    {
      QRect gutterRect(0, iTop, s_iGutterWidth, s_iRowHeight);
      p.fillRect(gutterRect, bSelected ? QColor(0x2b, 0x1e, 0x38) : QColor(0x2d, 0x2d, 0x2d));

      if (row.IsMerged())
      {
        // stacked axis colors instead of the kind square
        p.fillRect(QRect(8, iCenterY - 4, 8, 4), GetAxisColor(0));
        p.fillRect(QRect(8, iCenterY, 8, 4), GetAxisColor(1));
      }
      else
      {
        p.fillRect(QRect(8, iCenterY - 4, 8, 8), GetTargetColor(primary.m_iTarget));
      }

      p.setPen(primary.m_bElementFound ? QColor(0xcc, 0xcc, 0xcc) : QColor(0xE0, 0x9E, 0x3E));
      QFont font = p.font();
      font.setPointSizeF(8.0);
      p.setFont(font);

      plStringBuilder sLabel;
      sLabel.SetFormat("{} . {}", primary.m_sElement.IsEmpty() ? "?" : primary.m_sElement.GetData(), row.IsMerged() ? GetPairName(primary.m_iTarget) : GetTargetName(primary.m_iTarget));
      p.drawText(gutterRect.adjusted(22, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, QString::fromUtf8(sLabel.GetData()));
    }

    // lane background (mockup .tlrow .lane: window surface with a subtle row separator)
    {
      QRect laneRect(s_iGutterWidth, iTop, width() - s_iGutterWidth, s_iRowHeight);
      p.fillRect(laneRect, QColor(0x1e, 0x1e, 0x1e));
      p.setPen(QColor(0x26, 0x26, 0x26));
      p.drawLine(laneRect.left(), iTop + s_iRowHeight - 1, laneRect.right(), iTop + s_iRowHeight - 1);
      p.setPen(QColor(0x3c, 0x3c, 0x3c));
      p.drawLine(s_iGutterWidth - 1, iTop, s_iGutterWidth - 1, iTop + s_iRowHeight);
    }

    // shared value normalization across the row's tracks (vector pairs share a scale)
    float fMin = 0.0f, fMax = 0.0f;
    bool bAnyKey = false;

    for (plInt32 t = 0; t < 2; ++t)
    {
      if (row.m_iTracks[t] < 0)
        continue;

      for (const plQtUiAnimationsPanel::KeyInfo& key : pClip->m_Tracks[row.m_iTracks[t]].m_Keys)
      {
        if (!bAnyKey)
        {
          fMin = fMax = key.m_fValue;
          bAnyKey = true;
        }
        else
        {
          fMin = plMath::Min(fMin, key.m_fValue);
          fMax = plMath::Max(fMax, key.m_fValue);
        }
      }
    }

    const float fRange = fMax - fMin;
    const int iCurveTop = iTop + 5;
    const int iCurveBottom = iTop + s_iRowHeight - 5;

    auto valueToY = [&](float fValue) -> float
    {
      if (fRange <= 0.0001f)
        return static_cast<float>(iCenterY);
      return iCurveBottom - ((fValue - fMin) / fRange) * (iCurveBottom - iCurveTop);
    };

    for (plInt32 iTrackInRow = 0; iTrackInRow < 2; ++iTrackInRow)
    {
      if (row.m_iTracks[iTrackInRow] < 0)
        continue;

      const plQtUiAnimationsPanel::TrackInfo& track = pClip->m_Tracks[row.m_iTracks[iTrackInRow]];

      if (track.m_Keys.IsEmpty())
        continue;

      const QColor targetColor = row.IsMerged() ? GetAxisColor(iTrackInRow) : GetTargetColor(track.m_iTarget);
      const int iX0 = static_cast<int>(TimeToX(track.m_Keys[0].m_fTime));
      const int iX1 = static_cast<int>(TimeToX(track.m_Keys[track.m_Keys.GetCount() - 1].m_fTime));

      if (track.m_iTarget == plUiAnimTarget::Tint)
      {
        // color track: the lane shows the actual gradient between the keys
        if (iX1 > iX0)
        {
          QLinearGradient gradient(iX0, 0, iX1, 0);

          const float fFirst = track.m_Keys[0].m_fTime;
          const float fSpan = plMath::Max(track.m_Keys[track.m_Keys.GetCount() - 1].m_fTime - fFirst, 0.0001f);

          for (const plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
          {
            const plColorGammaUB gamma = key.m_Color;
            gradient.setColorAt(plMath::Clamp((key.m_fTime - fFirst) / fSpan, 0.0f, 1.0f), QColor(gamma.r, gamma.g, gamma.b, gamma.a));
          }

          const QRect gradientRect(iX0, iCenterY - 5, iX1 - iX0, 10);

          // checker under the gradient so alpha reads correctly
          p.fillRect(gradientRect, QColor(70, 70, 70));
          for (int x = gradientRect.left(); x < gradientRect.right(); x += 5)
          {
            p.fillRect(QRect(x, ((x / 5) % 2 == 0) ? gradientRect.top() : iCenterY, plMath::Min(5, gradientRect.right() - x), 5), QColor(110, 110, 110));
          }

          p.fillRect(gradientRect, gradient);
          p.setPen(QPen(targetColor, 1));
          p.drawRect(gradientRect);
        }
      }
      else
      {
        // float track: eased value curve on the row's shared scale
        QPainterPath curve;
        curve.moveTo(TimeToX(track.m_Keys[0].m_fTime), valueToY(track.m_Keys[0].m_fValue));

        for (plUInt32 uiKey = 1; uiKey < track.m_Keys.GetCount(); ++uiKey)
        {
          const plQtUiAnimationsPanel::KeyInfo& prev = track.m_Keys[uiKey - 1];
          const plQtUiAnimationsPanel::KeyInfo& next = track.m_Keys[uiKey];

          // sample the segment so the easing shape is visible
          constexpr int iSamples = 8;
          for (int iSample = 1; iSample <= iSamples; ++iSample)
          {
            const float fT = static_cast<float>(iSample) / iSamples;
            const float fTime = plMath::Lerp(prev.m_fTime, next.m_fTime, fT);
            const float fValue = plMath::Lerp(prev.m_fValue, next.m_fValue, ApplyEasingEditor(prev.m_iEasing, fT));
            curve.lineTo(TimeToX(fTime), valueToY(fValue));
          }
        }

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(targetColor, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curve);

        // faint baseline between first and last key
        QColor faint = targetColor;
        faint.setAlpha(60);
        p.setPen(QPen(faint, 1));
        p.drawLine(iX0, iCenterY, iX1, iCenterY);
        p.setRenderHint(QPainter::Antialiasing, false);
      }

      // keys as diamonds (on top of the lane visual)
      for (const plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
      {
        const int iX = static_cast<int>(TimeToX(key.m_fTime));
        const bool bKeySelected = m_pPanel->IsKeySelected(key.m_Guid);
        const int iKeyY = (track.m_iTarget == plUiAnimTarget::Tint) ? iCenterY : static_cast<int>(valueToY(key.m_fValue));

        QPolygon diamond;
        diamond << QPoint(iX, iKeyY - s_iKeyHalf) << QPoint(iX + s_iKeyHalf, iKeyY)
                << QPoint(iX, iKeyY + s_iKeyHalf) << QPoint(iX - s_iKeyHalf, iKeyY);

        if (track.m_iTarget == plUiAnimTarget::Tint)
        {
          // the diamond shows the KEY'S color
          const plColorGammaUB gamma = key.m_Color;
          p.setPen(QPen(bKeySelected ? palette().highlightedText().color() : QColor(0x0d, 0x0d, 0x0d), 1));
          p.setBrush(bKeySelected ? palette().highlight().color() : QColor(gamma.r, gamma.g, gamma.b));
        }
        else
        {
          p.setPen(QPen(bKeySelected ? palette().highlightedText().color() : targetColor, 1));
          p.setBrush(bKeySelected ? palette().highlight().color() : targetColor.darker(140));
        }

        p.drawPolygon(diamond);
      }
    }
  }

  // ————— playhead (mockup: --pl-brand-bright) —————
  {
    const int iX = static_cast<int>(TimeToX(plMath::Clamp(m_pPanel->GetPlayhead(), 0.0f, fDuration)));
    p.setPen(QPen(QColor(0x06, 0xfa, 0xc9), 2));
    p.drawLine(iX, 0, iX, height());
  }
}

void plQtUiTimelineWidget::mousePressEvent(QMouseEvent* pEvent)
{
  if (pEvent->button() != Qt::LeftButton)
  {
    QWidget::mousePressEvent(pEvent);
    return;
  }

  plUInt32 uiTrack = 0, uiKey = 0;

  if (HitTestKey(pEvent->pos(), uiTrack, uiKey))
  {
    plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

    if (m_OnSelectKey)
    {
      m_OnSelectKey(pClip->m_Tracks[uiTrack].m_Guid, pClip->m_Tracks[uiTrack].m_Keys[uiKey].m_Guid);
    }

    m_bDraggingKey = true;
    m_bKeyWasDragged = false;
  }
  else if (pEvent->pos().y() < s_iRulerHeight || pEvent->pos().x() >= s_iGutterWidth)
  {
    // ruler or empty lane: scrub the playhead
    const int iRow = TrackAtY(pEvent->pos().y());

    if (iRow >= 0 && m_OnSelectKey)
    {
      plHybridArray<TimelineRow, 16> rows;
      BuildTimelineRows(m_pPanel->GetSelectedClip(), rows);
      m_OnSelectKey(m_pPanel->GetSelectedClip()->m_Tracks[rows[iRow].m_iTracks[0]].m_Guid, plUuid());
    }

    m_bScrubbing = true;
    m_pPanel->SetPlayhead(m_pPanel->SnapTime(XToTime(static_cast<float>(pEvent->pos().x()))));

    if (m_OnScrub)
    {
      m_OnScrub(m_pPanel->GetPlayhead());
    }
  }
  else
  {
    // gutter: select the row (a vector pair selects its X component)
    const int iRow = TrackAtY(pEvent->pos().y());

    if (iRow >= 0 && m_OnSelectKey)
    {
      plHybridArray<TimelineRow, 16> rows;
      BuildTimelineRows(m_pPanel->GetSelectedClip(), rows);
      m_OnSelectKey(m_pPanel->GetSelectedClip()->m_Tracks[rows[iRow].m_iTracks[0]].m_Guid, plUuid());
    }
  }

  update();
}

void plQtUiTimelineWidget::mouseMoveEvent(QMouseEvent* pEvent)
{
  if (m_bDraggingKey)
  {
    // live-move the key in the panel's local copy; the document is written on release
    plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

    if (pClip != nullptr)
    {
      for (plQtUiAnimationsPanel::TrackInfo& track : pClip->m_Tracks)
      {
        for (plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
        {
          if (m_pPanel->IsKeySelected(key.m_Guid))
          {
            key.m_fTime = m_pPanel->SnapTime(XToTime(static_cast<float>(pEvent->pos().x())));
            m_bKeyWasDragged = true;
            update();
            return;
          }
        }
      }
    }
  }
  else if (m_bScrubbing)
  {
    m_pPanel->SetPlayhead(m_pPanel->SnapTime(XToTime(static_cast<float>(pEvent->pos().x()))));

    if (m_OnScrub)
    {
      m_OnScrub(m_pPanel->GetPlayhead());
    }

    update();
  }
}

void plQtUiTimelineWidget::mouseReleaseEvent(QMouseEvent* pEvent)
{
  if (m_bDraggingKey && m_bKeyWasDragged)
  {
    // commit the drag to the document
    const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

    if (pClip != nullptr && m_OnKeyMoved)
    {
      for (const plQtUiAnimationsPanel::TrackInfo& track : pClip->m_Tracks)
      {
        for (const plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
        {
          if (m_pPanel->IsKeySelected(key.m_Guid))
          {
            m_OnKeyMoved(key.m_Guid, key.m_fTime);
            break;
          }
        }
      }
    }
  }

  m_bDraggingKey = false;
  m_bScrubbing = false;
  m_bKeyWasDragged = false;
}

void plQtUiTimelineWidget::mouseDoubleClickEvent(QMouseEvent* pEvent)
{
  // double-click on a lane adds a key at that time (both components of a vector row)
  const int iRow = TrackAtY(pEvent->pos().y());

  if (iRow >= 0 && pEvent->pos().x() >= s_iGutterWidth)
  {
    plUInt32 uiHitTrack = 0, uiHitKey = 0;
    if (HitTestKey(pEvent->pos(), uiHitTrack, uiHitKey))
      return; // double-clicked an existing key

    if (m_OnAddKey)
    {
      plHybridArray<TimelineRow, 16> rows;
      BuildTimelineRows(m_pPanel->GetSelectedClip(), rows);

      const float fTime = m_pPanel->SnapTime(XToTime(static_cast<float>(pEvent->pos().x())));

      for (plInt32 iTrackInRow = 0; iTrackInRow < 2; ++iTrackInRow)
      {
        const plInt32 iTrack = rows[iRow].m_iTracks[iTrackInRow];

        if (iTrack >= 0)
        {
          m_OnAddKey(m_pPanel->GetSelectedClip()->m_Tracks[iTrack].m_Guid, fTime);
        }
      }
    }
  }
}

void plQtUiTimelineWidget::keyPressEvent(QKeyEvent* pEvent)
{
  if (pEvent->key() == Qt::Key_Delete)
  {
    const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

    if (pClip != nullptr && m_OnDeleteKey)
    {
      for (const plQtUiAnimationsPanel::TrackInfo& track : pClip->m_Tracks)
      {
        for (const plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
        {
          if (m_pPanel->IsKeySelected(key.m_Guid))
          {
            m_OnDeleteKey(key.m_Guid);
            return;
          }
        }
      }
    }
  }

  QWidget::keyPressEvent(pEvent);
}

//////////////////////////////////////////////////////////////////////////
// plQtUiCurveEditorWidget

namespace
{
  constexpr int s_iCurveMarginLeft = 44;  // value labels
  constexpr int s_iCurveMarginRight = 8;
  constexpr int s_iCurveMarginTop = 18; // "editing: ..." caption
  constexpr int s_iCurveMarginBottom = 8;
} // namespace

plQtUiCurveEditorWidget::plQtUiCurveEditorWidget(plQtUiAnimationsPanel* pPanel)
  : QWidget(pPanel)
  , m_pPanel(pPanel)
{
  setFixedHeight(150);
  setFocusPolicy(Qt::ClickFocus);
  setMouseTracking(true);
}

bool plQtUiCurveEditorWidget::GetEditedTracks(plInt32& out_iTrack0, plInt32& out_iTrack1) const
{
  out_iTrack0 = -1;
  out_iTrack1 = -1;

  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();
  if (pClip == nullptr || pClip->m_Tracks.IsEmpty())
    return false;

  plHybridArray<TimelineRow, 16> rows;
  BuildTimelineRows(pClip, rows);

  if (rows.IsEmpty())
    return false;

  // the row holding the selected track; the first row when nothing is selected
  for (const TimelineRow& row : rows)
  {
    for (plInt32 iTrackInRow = 0; iTrackInRow < 2; ++iTrackInRow)
    {
      if (row.m_iTracks[iTrackInRow] >= 0 && m_pPanel->IsTrackSelected(pClip->m_Tracks[row.m_iTracks[iTrackInRow]].m_Guid))
      {
        out_iTrack0 = row.m_iTracks[0];
        out_iTrack1 = row.m_iTracks[1];
        return true;
      }
    }
  }

  out_iTrack0 = rows[0].m_iTracks[0];
  out_iTrack1 = rows[0].m_iTracks[1];
  return true;
}

void plQtUiCurveEditorWidget::ComputeValueRange(float& out_fMin, float& out_fMax) const
{
  out_fMin = 0.0f;
  out_fMax = 1.0f;

  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

  plInt32 iTrack0 = -1, iTrack1 = -1;
  if (pClip == nullptr || !GetEditedTracks(iTrack0, iTrack1))
    return;

  float fMin = plMath::MaxValue<float>();
  float fMax = -plMath::MaxValue<float>();

  for (plInt32 iTrack : {iTrack0, iTrack1})
  {
    if (iTrack < 0)
      continue;

    for (const plQtUiAnimationsPanel::KeyInfo& key : pClip->m_Tracks[iTrack].m_Keys)
    {
      fMin = plMath::Min(fMin, key.m_fValue);
      fMax = plMath::Max(fMax, key.m_fValue);
    }
  }

  if (fMin > fMax) // no keys
  {
    fMin = 0.0f;
    fMax = 1.0f;
  }

  // include the zero line and pad so curves don't hug the borders
  fMin = plMath::Min(fMin, 0.0f);
  fMax = plMath::Max(fMax, 0.0f);

  const float fPad = plMath::Max((fMax - fMin) * 0.1f, 0.5f);
  out_fMin = fMin - fPad;
  out_fMax = fMax + fPad;
}

float plQtUiCurveEditorWidget::TimeToX(float fTime) const
{
  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();
  const float fDuration = (pClip != nullptr) ? plMath::Max(pClip->m_fDuration, 0.0001f) : 1.0f;
  return s_iCurveMarginLeft + (fTime / fDuration) * (width() - s_iCurveMarginLeft - s_iCurveMarginRight);
}

float plQtUiCurveEditorWidget::XToTime(float fX) const
{
  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();
  const float fDuration = (pClip != nullptr) ? plMath::Max(pClip->m_fDuration, 0.0001f) : 1.0f;
  const float fWidth = plMath::Max(static_cast<float>(width() - s_iCurveMarginLeft - s_iCurveMarginRight), 1.0f);
  return plMath::Clamp((fX - s_iCurveMarginLeft) / fWidth, 0.0f, 1.0f) * fDuration;
}

float plQtUiCurveEditorWidget::ValueToY(float fValue) const
{
  const float fRange = plMath::Max(m_fRangeMax - m_fRangeMin, 0.0001f);
  return (height() - s_iCurveMarginBottom) - ((fValue - m_fRangeMin) / fRange) * (height() - s_iCurveMarginTop - s_iCurveMarginBottom);
}

float plQtUiCurveEditorWidget::YToValue(float fY) const
{
  const float fSpan = plMath::Max(static_cast<float>(height() - s_iCurveMarginTop - s_iCurveMarginBottom), 1.0f);
  return m_fRangeMin + ((height() - s_iCurveMarginBottom) - fY) / fSpan * (m_fRangeMax - m_fRangeMin);
}

bool plQtUiCurveEditorWidget::HitTestKey(const QPoint& pos, plInt32& out_iTrack, plUInt32& out_uiKey) const
{
  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

  plInt32 iTrack0 = -1, iTrack1 = -1;
  if (pClip == nullptr || !GetEditedTracks(iTrack0, iTrack1))
    return false;

  const bool bTint = pClip->m_Tracks[iTrack0].m_iTarget == plUiAnimTarget::Tint;

  for (plInt32 iTrack : {iTrack0, iTrack1})
  {
    if (iTrack < 0)
      continue;

    const plQtUiAnimationsPanel::TrackInfo& track = pClip->m_Tracks[iTrack];

    for (plUInt32 uiKey = 0; uiKey < track.m_Keys.GetCount(); ++uiKey)
    {
      const float fX = TimeToX(track.m_Keys[uiKey].m_fTime);
      const float fY = bTint ? height() * 0.5f : ValueToY(track.m_Keys[uiKey].m_fValue);

      if (plMath::Abs(pos.x() - fX) <= 6.0f && plMath::Abs(pos.y() - fY) <= 6.0f)
      {
        out_iTrack = iTrack;
        out_uiKey = uiKey;
        return true;
      }
    }
  }

  return false;
}

void plQtUiCurveEditorWidget::paintEvent(QPaintEvent* pEvent)
{
  PL_IGNORE_UNUSED(pEvent);

  QPainter p(this);
  p.fillRect(rect(), QColor(0x14, 0x14, 0x14));

  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

  plInt32 iTrack0 = -1, iTrack1 = -1;
  if (pClip == nullptr || !GetEditedTracks(iTrack0, iTrack1))
  {
    p.setPen(QColor(0x8e, 0x8e, 0x8e));
    p.drawText(rect(), Qt::AlignCenter, plMakeQString(plTranslate("GameUi.Animations.CurveEditorHint")));
    return;
  }

  ComputeValueRange(m_fRangeMin, m_fRangeMax);

  const plQtUiAnimationsPanel::TrackInfo& track0 = pClip->m_Tracks[iTrack0];
  const bool bTint = track0.m_iTarget == plUiAnimTarget::Tint;
  const bool bMerged = iTrack1 >= 0;

  // caption: "editing: Element · Target" (mockup's dim right-side note)
  {
    plStringBuilder sCaption;
    const char* szTarget = bMerged ? GetPairName(track0.m_iTarget) : GetTargetName(track0.m_iTarget);
    sCaption.SetFormat("{}: {} \xC2\xB7 {}", plTranslate("GameUi.Animations.CurveEditing"), track0.m_sElement, szTarget);

    p.setPen(QColor(0x8e, 0x8e, 0x8e));
    p.drawText(QRect(s_iCurveMarginLeft, 2, width() - s_iCurveMarginLeft - s_iCurveMarginRight, 14), Qt::AlignLeft | Qt::AlignVCenter, sCaption.GetData());
  }

  const int iGraphTop = s_iCurveMarginTop;
  const int iGraphBottom = height() - s_iCurveMarginBottom;

  if (bTint)
  {
    // ————— gradient strip (checker under it so alpha reads) —————
    const int iStripTop = (iGraphTop + iGraphBottom) / 2 - 12;
    const QRect stripRect(static_cast<int>(TimeToX(0.0f)), iStripTop, static_cast<int>(TimeToX(pClip->m_fDuration) - TimeToX(0.0f)), 24);

    p.fillRect(stripRect, QColor(70, 70, 70));
    for (int x = stripRect.left(); x < stripRect.right(); x += 8)
    {
      p.fillRect(QRect(x, ((x / 8) % 2 == 0) ? stripRect.top() : stripRect.top() + 12, plMath::Min(8, stripRect.right() - x), 12), QColor(110, 110, 110));
    }

    if (!track0.m_Keys.IsEmpty())
    {
      QLinearGradient gradient(TimeToX(0.0f), 0, TimeToX(pClip->m_fDuration), 0);
      const float fDuration = plMath::Max(pClip->m_fDuration, 0.0001f);

      for (const plQtUiAnimationsPanel::KeyInfo& key : track0.m_Keys)
      {
        const plColorGammaUB gamma = key.m_Color;
        gradient.setColorAt(plMath::Clamp(key.m_fTime / fDuration, 0.0f, 1.0f), QColor(gamma.r, gamma.g, gamma.b, gamma.a));
      }

      p.fillRect(stripRect, gradient);
    }

    p.setPen(QPen(QColor(0x3c, 0x3c, 0x3c), 1));
    p.drawRect(stripRect);

    // key markers on the strip center line
    for (const plQtUiAnimationsPanel::KeyInfo& key : track0.m_Keys)
    {
      const int iX = static_cast<int>(TimeToX(key.m_fTime));
      const int iY = height() / 2;
      const bool bSelected = m_pPanel->IsKeySelected(key.m_Guid);
      const plColorGammaUB gamma = key.m_Color;

      QPolygon diamond;
      diamond << QPoint(iX, iY - 7) << QPoint(iX + 7, iY) << QPoint(iX, iY + 7) << QPoint(iX - 7, iY);

      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen(bSelected ? Qt::white : QColor(0x0d, 0x0d, 0x0d), bSelected ? 2 : 1));
      p.setBrush(QColor(gamma.r, gamma.g, gamma.b));
      p.drawPolygon(diamond);
      p.setRenderHint(QPainter::Antialiasing, false);
    }
  }
  else
  {
    // ————— value graph: grid, zero line, labels —————
    p.setPen(QColor(0x26, 0x26, 0x26));
    for (int i = 0; i <= 4; ++i)
    {
      const int iY = iGraphTop + i * (iGraphBottom - iGraphTop) / 4;
      p.drawLine(s_iCurveMarginLeft, iY, width() - s_iCurveMarginRight, iY);
    }

    // zero line (mockup's blue baseline)
    const int iZeroY = static_cast<int>(ValueToY(0.0f));
    p.setPen(QPen(QColor(0x3b, 0x74, 0xa0), 1));
    p.drawLine(s_iCurveMarginLeft, iZeroY, width() - s_iCurveMarginRight, iZeroY);

    // value labels (min / 0 / max)
    p.setPen(QColor(0x8e, 0x8e, 0x8e));
    p.drawText(QRect(0, iGraphTop - 6, s_iCurveMarginLeft - 6, 12), Qt::AlignRight | Qt::AlignVCenter, QString::number(m_fRangeMax, 'g', 3));
    p.drawText(QRect(0, iGraphBottom - 6, s_iCurveMarginLeft - 6, 12), Qt::AlignRight | Qt::AlignVCenter, QString::number(m_fRangeMin, 'g', 3));
    p.drawText(QRect(0, iZeroY - 6, s_iCurveMarginLeft - 6, 12), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0"));

    // ————— curves + keys —————
    for (plInt32 iTrackInRow = 0; iTrackInRow < 2; ++iTrackInRow)
    {
      const plInt32 iTrack = (iTrackInRow == 0) ? iTrack0 : iTrack1;
      if (iTrack < 0)
        continue;

      const plQtUiAnimationsPanel::TrackInfo& track = pClip->m_Tracks[iTrack];
      if (track.m_Keys.IsEmpty())
        continue;

      const QColor curveColor = bMerged ? GetAxisColor(iTrackInRow) : GetTargetColor(track.m_iTarget);

      QPainterPath curve;

      // flat extension before the first and after the last key
      curve.moveTo(TimeToX(0.0f), ValueToY(track.m_Keys[0].m_fValue));
      curve.lineTo(TimeToX(track.m_Keys[0].m_fTime), ValueToY(track.m_Keys[0].m_fValue));

      for (plUInt32 uiKey = 1; uiKey < track.m_Keys.GetCount(); ++uiKey)
      {
        const plQtUiAnimationsPanel::KeyInfo& prev = track.m_Keys[uiKey - 1];
        const plQtUiAnimationsPanel::KeyInfo& next = track.m_Keys[uiKey];

        constexpr int iSamples = 16;
        for (int iSample = 1; iSample <= iSamples; ++iSample)
        {
          const float fT = static_cast<float>(iSample) / iSamples;
          const float fTime = plMath::Lerp(prev.m_fTime, next.m_fTime, fT);
          const float fValue = plMath::Lerp(prev.m_fValue, next.m_fValue, ApplyEasingEditor(prev.m_iEasing, fT));
          curve.lineTo(TimeToX(fTime), ValueToY(fValue));
        }
      }

      curve.lineTo(TimeToX(pClip->m_fDuration), ValueToY(track.m_Keys[track.m_Keys.GetCount() - 1].m_fValue));

      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen(curveColor, 2));
      p.setBrush(Qt::NoBrush);
      p.drawPath(curve);

      for (const plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
      {
        const float fX = TimeToX(key.m_fTime);
        const float fY = ValueToY(key.m_fValue);
        const bool bSelected = m_pPanel->IsKeySelected(key.m_Guid);

        QPolygonF diamond;
        diamond << QPointF(fX, fY - 6) << QPointF(fX + 6, fY) << QPointF(fX, fY + 6) << QPointF(fX - 6, fY);

        p.setPen(QPen(bSelected ? Qt::white : curveColor, bSelected ? 2 : 1));
        p.setBrush(bSelected ? curveColor.lighter(150) : curveColor.darker(140));
        p.drawPolygon(diamond);
      }

      p.setRenderHint(QPainter::Antialiasing, false);
    }
  }

  // playhead for context (brand-bright, matches the timeline)
  {
    const int iX = static_cast<int>(TimeToX(plMath::Clamp(m_pPanel->GetPlayhead(), 0.0f, pClip->m_fDuration)));
    p.setPen(QPen(QColor(0x06, 0xfa, 0xc9), 1));
    p.drawLine(iX, iGraphTop - 4, iX, iGraphBottom + 4);
  }
}

void plQtUiCurveEditorWidget::mousePressEvent(QMouseEvent* pEvent)
{
  if (pEvent->button() != Qt::LeftButton)
  {
    QWidget::mousePressEvent(pEvent);
    return;
  }

  plInt32 iTrack = -1;
  plUInt32 uiKey = 0;

  if (HitTestKey(pEvent->pos(), iTrack, uiKey))
  {
    const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

    if (m_OnSelectKey)
    {
      m_OnSelectKey(pClip->m_Tracks[iTrack].m_Guid, pClip->m_Tracks[iTrack].m_Keys[uiKey].m_Guid);
    }

    m_bDraggingKey = true;
    m_bKeyWasDragged = false;
  }

  update();
}

void plQtUiCurveEditorWidget::mouseMoveEvent(QMouseEvent* pEvent)
{
  if (!m_bDraggingKey)
    return;

  plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();
  if (pClip == nullptr)
    return;

  for (plQtUiAnimationsPanel::TrackInfo& track : pClip->m_Tracks)
  {
    for (plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
    {
      if (m_pPanel->IsKeySelected(key.m_Guid))
      {
        key.m_fTime = m_pPanel->SnapTime(XToTime(static_cast<float>(pEvent->pos().x())));

        // Tint keys have no scalar value: the strip drag is time-only
        if (track.m_iTarget != plUiAnimTarget::Tint)
        {
          key.m_fValue = YToValue(static_cast<float>(pEvent->pos().y()));
        }

        m_bKeyWasDragged = true;
        update();
        return;
      }
    }
  }
}

void plQtUiCurveEditorWidget::mouseReleaseEvent(QMouseEvent* pEvent)
{
  PL_IGNORE_UNUSED(pEvent);

  if (m_bDraggingKey && m_bKeyWasDragged && m_OnKeyChanged)
  {
    const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

    if (pClip != nullptr)
    {
      for (const plQtUiAnimationsPanel::TrackInfo& track : pClip->m_Tracks)
      {
        for (const plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
        {
          if (m_pPanel->IsKeySelected(key.m_Guid))
          {
            m_OnKeyChanged(key.m_Guid);
            break;
          }
        }
      }
    }
  }

  m_bDraggingKey = false;
  m_bKeyWasDragged = false;
}

void plQtUiCurveEditorWidget::mouseDoubleClickEvent(QMouseEvent* pEvent)
{
  const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

  plInt32 iTrack0 = -1, iTrack1 = -1;
  if (pClip == nullptr || !GetEditedTracks(iTrack0, iTrack1) || !m_OnAddKey)
    return;

  plInt32 iHitTrack = -1;
  plUInt32 uiHitKey = 0;
  if (HitTestKey(pEvent->pos(), iHitTrack, uiHitKey))
    return; // double-clicked an existing key

  const float fTime = m_pPanel->SnapTime(XToTime(static_cast<float>(pEvent->pos().x())));

  // vector rows: add to the component whose curve is nearest to the click
  plInt32 iTargetTrack = iTrack0;

  if (iTrack1 >= 0)
  {
    auto valueAt = [&](const plQtUiAnimationsPanel::TrackInfo& track) -> float
    {
      if (track.m_Keys.IsEmpty())
        return 0.0f;

      for (plUInt32 uiKey = 0; uiKey < track.m_Keys.GetCount(); ++uiKey)
      {
        if (track.m_Keys[uiKey].m_fTime >= fTime)
        {
          if (uiKey == 0)
            return track.m_Keys[0].m_fValue;

          const plQtUiAnimationsPanel::KeyInfo& prev = track.m_Keys[uiKey - 1];
          const plQtUiAnimationsPanel::KeyInfo& next = track.m_Keys[uiKey];
          const float fT = (fTime - prev.m_fTime) / plMath::Max(next.m_fTime - prev.m_fTime, 0.0001f);
          return plMath::Lerp(prev.m_fValue, next.m_fValue, ApplyEasingEditor(prev.m_iEasing, fT));
        }
      }

      return track.m_Keys[track.m_Keys.GetCount() - 1].m_fValue;
    };

    const float fClickValue = YToValue(static_cast<float>(pEvent->pos().y()));
    const float fDist0 = plMath::Abs(valueAt(pClip->m_Tracks[iTrack0]) - fClickValue);
    const float fDist1 = plMath::Abs(valueAt(pClip->m_Tracks[iTrack1]) - fClickValue);

    iTargetTrack = (fDist1 < fDist0) ? iTrack1 : iTrack0;
  }

  m_OnAddKey(pClip->m_Tracks[iTargetTrack].m_Guid, fTime);
}

void plQtUiCurveEditorWidget::keyPressEvent(QKeyEvent* pEvent)
{
  if (pEvent->key() == Qt::Key_Delete && m_OnDeleteKey)
  {
    const plQtUiAnimationsPanel::ClipInfo* pClip = m_pPanel->GetSelectedClip();

    if (pClip != nullptr)
    {
      for (const plQtUiAnimationsPanel::TrackInfo& track : pClip->m_Tracks)
      {
        for (const plQtUiAnimationsPanel::KeyInfo& key : track.m_Keys)
        {
          if (m_pPanel->IsKeySelected(key.m_Guid))
          {
            m_OnDeleteKey(key.m_Guid);
            return;
          }
        }
      }
    }
  }

  QWidget::keyPressEvent(pEvent);
}

//////////////////////////////////////////////////////////////////////////
// plQtUiAnimationsPanel

plQtUiAnimationsPanel::plQtUiAnimationsPanel(QWidget* pParent)
  : QWidget(pParent)
{
  QHBoxLayout* pLayout = new QHBoxLayout(this);
  pLayout->setContentsMargins(4, 4, 4, 4);
  pLayout->setSpacing(8);

  // ————— left: clip list —————
  {
    QVBoxLayout* pLeft = nullptr;
    QFrame* pPanel = plUiEditorTheme::CreatePanel(this, plUiEditorTheme::szAccentAnim, plMakeQString(plTranslate("GameUi.Animations.Clips")), pLeft);
    pPanel->setMaximumWidth(300);

    m_pClipList = new QListWidget(this);
    m_pClipList->setStyleSheet(plUiEditorTheme::szListStyle);
    m_pClipList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_pClipList, &QListWidget::currentRowChanged, this, [this](int) { OnClipSelectionChanged(); });
    connect(m_pClipList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*)
      {
        if (const ClipInfo* pClip = GetSelectedClip())
        {
          Q_EMIT ClipActivated(pClip->m_Guid);
        }
      });
    pLeft->addWidget(m_pClipList, 1);

    QHBoxLayout* pButtons = new QHBoxLayout();

    QPushButton* pAdd = new QPushButton(plMakeQString(plTranslate("GameUi.Animations.AddClip")), this);
    connect(pAdd, &QPushButton::clicked, this, [this]() { Q_EMIT AddClipRequested(); });
    pButtons->addWidget(pAdd);

    QPushButton* pRemove = new QPushButton(plMakeQString(plTranslate("GameUi.Animations.RemoveClip")), this);
    connect(pRemove, &QPushButton::clicked, this, [this]()
      {
        if (const ClipInfo* pClip = GetSelectedClip())
        {
          Q_EMIT RemoveClipRequested(pClip->m_Guid);
        }
      });
    pButtons->addWidget(pRemove);

    pLeft->addLayout(pButtons);

    pLayout->addWidget(pPanel);
  }

  // ————— right: timeline —————
  {
    QVBoxLayout* pRight = nullptr;
    QHBoxLayout* pHeaderRow = nullptr;
    QFrame* pPanel = plUiEditorTheme::CreatePanel(this, plUiEditorTheme::szAccentAnim, plMakeQString(plTranslate("GameUi.Panel.Animations")), pRight, &pHeaderRow);

    // header strip: clip name + duration (editable inline) + trigger summary + transport
    {
      m_pClipNameEdit = new QLineEdit(this);
      m_pClipNameEdit->setFixedWidth(140);
      m_pClipNameEdit->setPlaceholderText(plMakeQString(plTranslate("GameUi.Animations.DlgName")));
      connect(m_pClipNameEdit, &QLineEdit::editingFinished, this, [this]()
        {
          const ClipInfo* pClip = GetSelectedClip();
          if (!m_bIgnoreChanges && pClip != nullptr && m_pClipNameEdit->text() != QString::fromUtf8(pClip->m_sName.GetData()))
          {
            Q_EMIT ClipRenamed(pClip->m_Guid, m_pClipNameEdit->text());
          }
        });
      pHeaderRow->addWidget(m_pClipNameEdit);

      m_pClipDurationSpin = new QDoubleSpinBox(this);
      m_pClipDurationSpin->setRange(0.01, 3600.0);
      m_pClipDurationSpin->setDecimals(2);
      m_pClipDurationSpin->setSingleStep(0.1);
      m_pClipDurationSpin->setSuffix(" s");
      connect(m_pClipDurationSpin, &QDoubleSpinBox::editingFinished, this, [this]()
        {
          const ClipInfo* pClip = GetSelectedClip();
          if (!m_bIgnoreChanges && pClip != nullptr && plMath::Abs(static_cast<float>(m_pClipDurationSpin->value()) - pClip->m_fDuration) > 0.001f)
          {
            Q_EMIT ClipDurationChanged(pClip->m_Guid, static_cast<float>(m_pClipDurationSpin->value()));
          }
        });
      pHeaderRow->addWidget(m_pClipDurationSpin);

      m_pHeaderLabel = new QLabel(this);
      m_pHeaderLabel->setStyleSheet("font-size: 10px; letter-spacing: 1px; color: #8e8e8e;");
      pHeaderRow->addWidget(m_pHeaderLabel, 1);

      // transport (sequencer-style): |< << Play >> >| + timecode + snap + Stop
      auto addTransportButton = [&](const char* szLabel, const char* szTooltipKey)
      {
        QPushButton* pButton = new QPushButton(QString::fromUtf8(szLabel), this);
        pButton->setToolTip(plMakeQString(plTranslate(szTooltipKey)));
        pButton->setFixedWidth(30);
        pHeaderRow->addWidget(pButton);
        return pButton;
      };

      QPushButton* pJumpStart = addTransportButton("|<", "GameUi.Animations.JumpStartTooltip");
      connect(pJumpStart, &QPushButton::clicked, this, [this]() { JumpToTime(0.0f); });

      QPushButton* pPrevKey = addTransportButton("<<", "GameUi.Animations.PrevKeyTooltip");
      connect(pPrevKey, &QPushButton::clicked, this, [this]() { StepToNeighborKey(false); });

      QPushButton* pPlay = new QPushButton(plMakeQString(plTranslate("GameUi.Animations.Play")), this);
      pPlay->setToolTip(plMakeQString(plTranslate("GameUi.Animations.PlayTooltip")));
      connect(pPlay, &QPushButton::clicked, this, [this]()
        {
          if (const ClipInfo* pClip = GetSelectedClip())
          {
            Q_EMIT PlayRequested(QString::fromUtf8(pClip->m_sName.GetData()));
          }
        });
      pHeaderRow->addWidget(pPlay);

      QPushButton* pNextKey = addTransportButton(">>", "GameUi.Animations.NextKeyTooltip");
      connect(pNextKey, &QPushButton::clicked, this, [this]() { StepToNeighborKey(true); });

      QPushButton* pJumpEnd = addTransportButton(">|", "GameUi.Animations.JumpEndTooltip");
      connect(pJumpEnd, &QPushButton::clicked, this, [this]()
        {
          if (const ClipInfo* pClip = GetSelectedClip())
          {
            JumpToTime(pClip->m_fDuration);
          }
        });

      // timecode readout (mockup's "00:03.400 f 102 / 00:08.000")
      m_pTimecodeLabel = new QLabel(this);
      m_pTimecodeLabel->setStyleSheet("font-family: Consolas, monospace; font-size: 10px; color: #06fac9; background: #191919; border: 1px solid #3c3c3c; border-radius: 3px; padding: 2px 6px;");
      pHeaderRow->addWidget(m_pTimecodeLabel);

      // key/scrub snapping grid (mockup's "Snap: Frame")
      m_pSnapCombo = new QComboBox(this);
      m_pSnapCombo->setToolTip(plMakeQString(plTranslate("GameUi.Animations.SnapTooltip")));
      m_pSnapCombo->addItem(plMakeQString(plTranslate("GameUi.Animations.SnapFrame")), 1.0f / 30.0f);
      m_pSnapCombo->addItem(plMakeQString(plTranslate("GameUi.Animations.SnapTenMs")), 0.01f);
      m_pSnapCombo->addItem(plMakeQString(plTranslate("GameUi.Animations.SnapOff")), 0.0f);
      pHeaderRow->addWidget(m_pSnapCombo);

      QPushButton* pStop = new QPushButton(plMakeQString(plTranslate("GameUi.Animations.Stop")), this);
      connect(pStop, &QPushButton::clicked, this, [this]() { Q_EMIT StopRequested(); });
      pHeaderRow->addWidget(pStop);
    }

    m_pTimeline = new plQtUiTimelineWidget(this);
    m_pTimeline->m_OnSelectKey = [this](plUuid trackGuid, plUuid keyGuid) { OnSelectKey(trackGuid, keyGuid); };
    m_pTimeline->m_OnKeyMoved = [this](plUuid keyGuid, float fTime) { Q_EMIT KeyMoved(keyGuid, fTime); };
    m_pTimeline->m_OnAddKey = [this](plUuid trackGuid, float fTime) { Q_EMIT AddKeyRequested(trackGuid, fTime); };
    m_pTimeline->m_OnDeleteKey = [this](plUuid keyGuid) { Q_EMIT RemoveKeyRequested(keyGuid); };
    m_pTimeline->m_OnScrub = [this](float fTime)
    {
      if (const ClipInfo* pClip = GetSelectedClip())
      {
        Q_EMIT Scrubbed(QString::fromUtf8(pClip->m_sName.GetData()), fTime);
      }
    };
    pRight->addWidget(m_pTimeline, 1);

    // curve/gradient editor for the selected row (sequencer-mockup bottom pane)
    m_pCurveEditor = new plQtUiCurveEditorWidget(this);
    m_pCurveEditor->m_OnSelectKey = [this](plUuid trackGuid, plUuid keyGuid) { OnSelectKey(trackGuid, keyGuid); };
    m_pCurveEditor->m_OnKeyChanged = [this](plUuid keyGuid) { CommitKeyFromLocal(keyGuid); };
    m_pCurveEditor->m_OnAddKey = [this](plUuid trackGuid, float fTime) { Q_EMIT AddKeyRequested(trackGuid, fTime); };
    m_pCurveEditor->m_OnDeleteKey = [this](plUuid keyGuid) { Q_EMIT RemoveKeyRequested(keyGuid); };
    pRight->addWidget(m_pCurveEditor);

    // key editor + track buttons
    {
      QHBoxLayout* pBottom = new QHBoxLayout();

      QPushButton* pAddTrack = new QPushButton(plMakeQString(plTranslate("GameUi.Animations.AddTrack")), this);
      connect(pAddTrack, &QPushButton::clicked, this, [this]()
        {
          if (const ClipInfo* pClip = GetSelectedClip())
          {
            Q_EMIT AddTrackRequested(pClip->m_Guid);
          }
        });
      pBottom->addWidget(pAddTrack);

      m_pRemoveTrackButton = new QPushButton(plMakeQString(plTranslate("GameUi.Animations.RemoveTrack")), this);
      connect(m_pRemoveTrackButton, &QPushButton::clicked, this, [this]()
        {
          if (!m_SelectedTrack.IsValid())
            return;

          // a merged vector row removes both of its component tracks
          plUuid pairGuid;

          if (const ClipInfo* pClip = GetSelectedClip())
          {
            for (const TrackInfo& track : pClip->m_Tracks)
            {
              if (track.m_Guid != m_SelectedTrack)
                continue;

              const plInt32 iPairTarget = GetPairTarget(track.m_iTarget);

              if (iPairTarget >= 0)
              {
                for (const TrackInfo& other : pClip->m_Tracks)
                {
                  if (other.m_iTarget == iPairTarget && other.m_sElement == track.m_sElement)
                  {
                    pairGuid = other.m_Guid;
                    break;
                  }
                }
              }

              break;
            }
          }

          Q_EMIT RemoveTrackRequested(m_SelectedTrack);

          if (pairGuid.IsValid())
          {
            Q_EMIT RemoveTrackRequested(pairGuid);
          }
        });
      pBottom->addWidget(m_pRemoveTrackButton);

      pBottom->addSpacing(16);

      // key editor (enabled while a key is selected)
      m_pKeyEditor = new QWidget(this);
      QHBoxLayout* pKeyLayout = new QHBoxLayout(m_pKeyEditor);
      pKeyLayout->setContentsMargins(0, 0, 0, 0);

      pKeyLayout->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Animations.KeyTime")), m_pKeyEditor));
      m_pKeyTimeSpin = new QDoubleSpinBox(m_pKeyEditor);
      m_pKeyTimeSpin->setRange(0.0, 3600.0);
      m_pKeyTimeSpin->setDecimals(2);
      m_pKeyTimeSpin->setSingleStep(0.05);
      m_pKeyTimeSpin->setSuffix(" s");
      connect(m_pKeyTimeSpin, &QDoubleSpinBox::editingFinished, this, [this]() { EmitKeyEdited(); });
      pKeyLayout->addWidget(m_pKeyTimeSpin);

      pKeyLayout->addWidget(new QLabel(plMakeQString(plTranslate("GameUi.Animations.KeyValue")), m_pKeyEditor));
      m_pKeyValueSpin = new QDoubleSpinBox(m_pKeyEditor);
      m_pKeyValueSpin->setRange(-100000.0, 100000.0);
      m_pKeyValueSpin->setDecimals(2);
      connect(m_pKeyValueSpin, &QDoubleSpinBox::editingFinished, this, [this]() { EmitKeyEdited(); });
      pKeyLayout->addWidget(m_pKeyValueSpin);

      m_pKeyColorButton = new QPushButton(m_pKeyEditor);
      m_pKeyColorButton->setFixedWidth(40);
      m_pKeyColorButton->setToolTip(plMakeQString(plTranslate("GameUi.Animations.KeyColorTooltip")));
      connect(m_pKeyColorButton, &QPushButton::clicked, this, [this]()
        {
          const plColorGammaUB gamma = m_KeyColor;
          QColor qtColor(gamma.r, gamma.g, gamma.b, gamma.a);

          const QColor picked = QColorDialog::getColor(qtColor, this, QString(), QColorDialog::ShowAlphaChannel);
          if (picked.isValid())
          {
            m_KeyColor = plColorGammaUB(static_cast<plUInt8>(picked.red()), static_cast<plUInt8>(picked.green()), static_cast<plUInt8>(picked.blue()), static_cast<plUInt8>(picked.alpha()));
            EmitKeyEdited();
          }
        });
      pKeyLayout->addWidget(m_pKeyColorButton);

      m_pKeyEasingCombo = new QComboBox(m_pKeyEditor);
      m_pKeyEasingCombo->addItem(plMakeQString(plTranslate("plUiAnimEasing::Linear")), 0);
      m_pKeyEasingCombo->addItem(plMakeQString(plTranslate("plUiAnimEasing::EaseIn")), 1);
      m_pKeyEasingCombo->addItem(plMakeQString(plTranslate("plUiAnimEasing::EaseOut")), 2);
      m_pKeyEasingCombo->addItem(plMakeQString(plTranslate("plUiAnimEasing::EaseInOut")), 3);
      connect(m_pKeyEasingCombo, &QComboBox::activated, this, [this](int) { EmitKeyEdited(); });
      pKeyLayout->addWidget(m_pKeyEasingCombo);

      QPushButton* pRemoveKey = new QPushButton(plMakeQString(plTranslate("GameUi.Animations.RemoveKey")), m_pKeyEditor);
      connect(pRemoveKey, &QPushButton::clicked, this, [this]()
        {
          if (m_SelectedKey.IsValid())
          {
            Q_EMIT RemoveKeyRequested(m_SelectedKey);
          }
        });
      pKeyLayout->addWidget(pRemoveKey);

      pBottom->addWidget(m_pKeyEditor);
      pBottom->addStretch();

      QLabel* pHint = new QLabel(plMakeQString(plTranslate("GameUi.Animations.Hint")), this);
      pHint->setStyleSheet(plUiEditorTheme::szHintText);
      pBottom->addWidget(pHint);

      pRight->addLayout(pBottom);
    }

    pLayout->addWidget(pPanel, 1);
  }

  RefreshKeyEditor();
}

void plQtUiAnimationsPanel::SetClips(plArrayPtr<const ClipInfo> clips)
{
  m_bIgnoreChanges = true;

  m_Clips.Clear();
  m_Clips.PushBackRange(clips);

  // drop stale selection
  {
    bool bClipFound = false, bTrackFound = false, bKeyFound = false;

    for (const ClipInfo& clip : m_Clips)
    {
      if (clip.m_Guid == m_SelectedClip)
        bClipFound = true;

      for (const TrackInfo& track : clip.m_Tracks)
      {
        if (track.m_Guid == m_SelectedTrack)
          bTrackFound = true;

        for (const KeyInfo& key : track.m_Keys)
        {
          if (key.m_Guid == m_SelectedKey)
            bKeyFound = true;
        }
      }
    }

    if (!bClipFound)
      m_SelectedClip = (m_Clips.IsEmpty()) ? plUuid() : m_Clips[0].m_Guid;
    if (!bTrackFound)
      m_SelectedTrack = plUuid();
    if (!bKeyFound)
      m_SelectedKey = plUuid();
  }

  RefreshClipList();
  RefreshHeader();
  RefreshKeyEditor();

  m_pTimeline->updateGeometry();
  m_pTimeline->update();
  m_pCurveEditor->update();

  m_bIgnoreChanges = false;
}

void plQtUiAnimationsPanel::RefreshClipList()
{
  m_pClipList->clear();

  int iSelectRow = -1;
  plStringBuilder tmp;

  for (plUInt32 i = 0; i < m_Clips.GetCount(); ++i)
  {
    const ClipInfo& clip = m_Clips[i];

    tmp.SetFormat("{} - {}s", clip.m_sName.IsEmpty() ? "<unnamed>" : clip.m_sName.GetData(), plArgF(clip.m_fDuration, 2));

    if (!clip.m_sPlayOnEvent.IsEmpty())
    {
      tmp.AppendFormat("  [{} {}]", plTranslate("GameUi.Animations.PlaysOn"), clip.m_sPlayOnEvent);
    }

    if (!clip.m_sEntry.IsEmpty())
    {
      tmp.AppendFormat("  [{}]", clip.m_sEntry);
    }

    m_pClipList->addItem(QString::fromUtf8(tmp.GetData()));

    if (clip.m_Guid == m_SelectedClip)
    {
      iSelectRow = static_cast<int>(i);
    }
  }

  if (iSelectRow >= 0)
  {
    m_pClipList->setCurrentRow(iSelectRow);
  }
}

void plQtUiAnimationsPanel::RefreshHeader()
{
  const ClipInfo* pClip = GetSelectedClip();

  const bool bWasIgnoring = m_bIgnoreChanges;
  m_bIgnoreChanges = true;

  m_pClipNameEdit->setEnabled(pClip != nullptr);
  m_pClipDurationSpin->setEnabled(pClip != nullptr);

  if (pClip == nullptr)
  {
    m_pClipNameEdit->setText(QString());
    m_pHeaderLabel->setText(plMakeQString(plTranslate("GameUi.Animations.NoClip")));
    m_bIgnoreChanges = bWasIgnoring;
    return;
  }

  m_pClipNameEdit->setText(QString::fromUtf8(pClip->m_sName.GetData()));
  m_pClipDurationSpin->setValue(pClip->m_fDuration);

  plStringBuilder tmp;

  if (!pClip->m_sPlayOnEvent.IsEmpty())
  {
    tmp.AppendFormat("{} {}", plTranslate("GameUi.Animations.PlaysOn"), pClip->m_sPlayOnEvent);
  }

  if (!pClip->m_sEntry.IsEmpty())
  {
    tmp.AppendFormat("{}{} {} ({})", tmp.IsEmpty() ? "" : " - ", plTranslate("GameUi.Animations.DrivenBy"), pClip->m_sEntry, plTranslate(pClip->m_bValueMode ? "GameUi.Animations.ModeValue" : "GameUi.Animations.ModeTrigger"));
  }

  if (tmp.IsEmpty())
  {
    tmp = plTranslate("GameUi.Animations.NoTriggerHint");
  }

  m_pHeaderLabel->setText(QString::fromUtf8(tmp.GetData()));
  UpdateTimecode();
  m_bIgnoreChanges = bWasIgnoring;
}

plQtUiAnimationsPanel::ClipInfo* plQtUiAnimationsPanel::GetSelectedClip()
{
  for (ClipInfo& clip : m_Clips)
  {
    if (clip.m_Guid == m_SelectedClip)
      return &clip;
  }

  return nullptr;
}

const plQtUiAnimationsPanel::ClipInfo* plQtUiAnimationsPanel::GetSelectedClip() const
{
  for (const ClipInfo& clip : m_Clips)
  {
    if (clip.m_Guid == m_SelectedClip)
      return &clip;
  }

  return nullptr;
}

void plQtUiAnimationsPanel::SetPlayhead(float fTime)
{
  const ClipInfo* pClip = GetSelectedClip();
  const float fDuration = (pClip != nullptr) ? plMath::Max(pClip->m_fDuration, 0.0001f) : 1.0f;
  m_fPlayhead = plMath::Clamp(fTime, 0.0f, fDuration);

  UpdateTimecode();

  if (m_pCurveEditor != nullptr)
  {
    m_pCurveEditor->update();
  }
}

void plQtUiAnimationsPanel::CommitKeyFromLocal(const plUuid& keyGuid)
{
  const ClipInfo* pClip = GetSelectedClip();
  if (pClip == nullptr)
    return;

  for (const TrackInfo& track : pClip->m_Tracks)
  {
    for (const KeyInfo& key : track.m_Keys)
    {
      if (key.m_Guid == keyGuid)
      {
        Q_EMIT KeyEdited(keyGuid, key.m_fTime, key.m_fValue, key.m_Color, key.m_iEasing);
        return;
      }
    }
  }
}

float plQtUiAnimationsPanel::SnapTime(float fTime) const
{
  const float fStep = (m_pSnapCombo != nullptr) ? m_pSnapCombo->currentData().toFloat() : 0.0f;

  if (fStep <= 0.0f)
    return fTime;

  return plMath::Round(fTime / fStep) * fStep;
}

void plQtUiAnimationsPanel::UpdateTimecode()
{
  if (m_pTimecodeLabel == nullptr)
    return;

  const ClipInfo* pClip = GetSelectedClip();

  if (pClip == nullptr)
  {
    m_pTimecodeLabel->setText(QStringLiteral("--:--.-- / --:--.--"));
    return;
  }

  auto formatTime = [](float fSeconds)
  {
    const int iMinutes = static_cast<int>(fSeconds) / 60;
    const float fRemainder = fSeconds - iMinutes * 60;
    return QString("%1:%2").arg(iMinutes, 2, 10, QChar('0')).arg(fRemainder, 5, 'f', 2, QChar('0'));
  };

  const int iFrame = static_cast<int>(plMath::Round(m_fPlayhead * 30.0f));

  m_pTimecodeLabel->setText(QString("%1 f %2 / %3").arg(formatTime(m_fPlayhead)).arg(iFrame).arg(formatTime(pClip->m_fDuration)));
}

void plQtUiAnimationsPanel::JumpToTime(float fTime)
{
  const ClipInfo* pClip = GetSelectedClip();
  if (pClip == nullptr)
    return;

  SetPlayhead(fTime);
  m_pTimeline->update();

  Q_EMIT Scrubbed(QString::fromUtf8(pClip->m_sName.GetData()), m_fPlayhead);
}

void plQtUiAnimationsPanel::StepToNeighborKey(bool bNext)
{
  const ClipInfo* pClip = GetSelectedClip();
  if (pClip == nullptr)
    return;

  constexpr float fEpsilon = 0.0005f;
  float fBest = bNext ? pClip->m_fDuration : 0.0f;
  bool bFound = false;

  for (const TrackInfo& track : pClip->m_Tracks)
  {
    for (const KeyInfo& key : track.m_Keys)
    {
      if (bNext)
      {
        if (key.m_fTime > m_fPlayhead + fEpsilon && (!bFound || key.m_fTime < fBest))
        {
          fBest = key.m_fTime;
          bFound = true;
        }
      }
      else
      {
        if (key.m_fTime < m_fPlayhead - fEpsilon && (!bFound || key.m_fTime > fBest))
        {
          fBest = key.m_fTime;
          bFound = true;
        }
      }
    }
  }

  // no neighbor key: fall to the clip bounds
  JumpToTime(bFound ? fBest : (bNext ? pClip->m_fDuration : 0.0f));
}

void plQtUiAnimationsPanel::OnClipSelectionChanged()
{
  if (m_bIgnoreChanges)
    return;

  const int iRow = m_pClipList->currentRow();

  if (iRow >= 0 && iRow < static_cast<int>(m_Clips.GetCount()))
  {
    m_SelectedClip = m_Clips[iRow].m_Guid;
  }
  else
  {
    m_SelectedClip = plUuid();
  }

  m_SelectedTrack = plUuid();
  m_SelectedKey = plUuid();
  m_fPlayhead = 0.0f;

  RefreshHeader();
  RefreshKeyEditor();
  m_pTimeline->updateGeometry();
  m_pTimeline->update();
  m_pCurveEditor->update();

  // clear any scrub preview from the previously selected clip
  Q_EMIT StopRequested();
}

void plQtUiAnimationsPanel::OnSelectKey(plUuid trackGuid, plUuid keyGuid)
{
  m_SelectedTrack = trackGuid;
  m_SelectedKey = keyGuid;
  RefreshKeyEditor();
  m_pTimeline->update();
  m_pCurveEditor->update();
}

const plQtUiAnimationsPanel::KeyInfo* plQtUiAnimationsPanel::FindSelectedKey() const
{
  if (!m_SelectedKey.IsValid())
    return nullptr;

  for (const ClipInfo& clip : m_Clips)
  {
    for (const TrackInfo& track : clip.m_Tracks)
    {
      for (const KeyInfo& key : track.m_Keys)
      {
        if (key.m_Guid == m_SelectedKey)
          return &key;
      }
    }
  }

  return nullptr;
}

void plQtUiAnimationsPanel::RefreshKeyEditor()
{
  const KeyInfo* pKey = FindSelectedKey();

  m_pKeyEditor->setEnabled(pKey != nullptr);
  m_pRemoveTrackButton->setEnabled(m_SelectedTrack.IsValid());

  m_bIgnoreChanges = true;

  if (pKey != nullptr)
  {
    m_pKeyTimeSpin->setValue(pKey->m_fTime);
    m_pKeyValueSpin->setValue(pKey->m_fValue);
    m_KeyColor = pKey->m_Color;
    m_pKeyEasingCombo->setCurrentIndex(plMath::Clamp(pKey->m_iEasing, 0, 3));
  }

  // color button swatch
  {
    const plColorGammaUB gamma = m_KeyColor;
    m_pKeyColorButton->setStyleSheet(QStringLiteral("background-color: rgba(%1,%2,%3,%4);").arg(gamma.r).arg(gamma.g).arg(gamma.b).arg(gamma.a));
  }

  m_bIgnoreChanges = false;
}

void plQtUiAnimationsPanel::EmitKeyEdited()
{
  if (m_bIgnoreChanges || !m_SelectedKey.IsValid())
    return;

  Q_EMIT KeyEdited(m_SelectedKey, static_cast<float>(m_pKeyTimeSpin->value()), static_cast<float>(m_pKeyValueSpin->value()), m_KeyColor, m_pKeyEasingCombo->currentData().toInt());
}
