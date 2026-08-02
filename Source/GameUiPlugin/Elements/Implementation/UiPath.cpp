#include <GameUiPlugin/GameUiPluginPCH.h>

#include <Foundation/Threading/Lock.h>
#include <Foundation/Threading/Mutex.h>
#include <GameUiPlugin/Elements/UiElements.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plUiPathStretch, 1)
  PL_ENUM_CONSTANTS(plUiPathStretch::Uniform, plUiPathStretch::Fill, plUiPathStretch::None)
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plUiPath, 1, plRTTIDefaultAllocator<plUiPath>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("PathData", GetPathData, SetPathData),
    PL_ENUM_MEMBER_PROPERTY("Stretch", plUiPathStretch, m_Stretch),
  }
  PL_END_PROPERTIES;

  PL_BEGIN_ATTRIBUTES
  {
    new plTitleAttribute("Vector Shape: {Name}"),
  }
  PL_END_ATTRIBUTES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plUiPath::plUiPath() = default;
plUiPath::~plUiPath() = default;

void plUiPath::SetPathData(const char* szData)
{
  m_sPathData = szData;
  m_bTessellated = false;
  m_Triangles.Clear();
}

// ═══════════════════════════════════════════════════════════════════════════════════════
// path-data parsing (SVG / XAML mini-language subset)
// ═══════════════════════════════════════════════════════════════════════════════════════

namespace
{
  constexpr plUInt32 s_uiMaxPathPoints = 8192;

  struct PathParser
  {
    const char* m_szCur = nullptr;
    const char* m_szEnd = nullptr;

    void SkipSeparators()
    {
      while (m_szCur < m_szEnd && (*m_szCur == ' ' || *m_szCur == ',' || *m_szCur == '\t' || *m_szCur == '\r' || *m_szCur == '\n'))
        ++m_szCur;
    }

    bool AtEnd()
    {
      SkipSeparators();
      return m_szCur >= m_szEnd;
    }

    bool PeekIsNumber()
    {
      SkipSeparators();
      if (m_szCur >= m_szEnd)
        return false;
      const char c = *m_szCur;
      return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
    }

    bool ReadFloat(float& out_fValue)
    {
      SkipSeparators();

      const char* szStart = m_szCur;
      if (m_szCur < m_szEnd && (*m_szCur == '-' || *m_szCur == '+'))
        ++m_szCur;
      while (m_szCur < m_szEnd && ((*m_szCur >= '0' && *m_szCur <= '9') || *m_szCur == '.'))
        ++m_szCur;
      if (m_szCur < m_szEnd && (*m_szCur == 'e' || *m_szCur == 'E'))
      {
        ++m_szCur;
        if (m_szCur < m_szEnd && (*m_szCur == '-' || *m_szCur == '+'))
          ++m_szCur;
        while (m_szCur < m_szEnd && *m_szCur >= '0' && *m_szCur <= '9')
          ++m_szCur;
      }

      if (m_szCur == szStart)
        return false;

      double fValue = 0.0;
      if (plConversionUtils::StringToFloat(plStringView(szStart, m_szCur), fValue).Failed())
        return false;

      out_fValue = static_cast<float>(fValue);
      return true;
    }

    bool ReadPoint(plVec2& out_vPoint)
    {
      return ReadFloat(out_vPoint.x) && ReadFloat(out_vPoint.y);
    }
  };

  using SubPath = plDynamicArray<plVec2>;

  void AddPoint(SubPath& ref_subPath, const plVec2& vPoint, plUInt32& ref_uiTotalPoints)
  {
    if (ref_uiTotalPoints >= s_uiMaxPathPoints)
      return;

    // skip duplicates (flattening steps can produce them)
    if (!ref_subPath.IsEmpty() && (ref_subPath.PeekBack() - vPoint).GetLengthSquared() < 0.0001f)
      return;

    ref_subPath.PushBack(vPoint);
    ++ref_uiTotalPoints;
  }

  void FlattenCubic(SubPath& ref_subPath, const plVec2& vP0, const plVec2& vP1, const plVec2& vP2, const plVec2& vP3, plUInt32& ref_uiTotalPoints)
  {
    constexpr plUInt32 uiSteps = 16;
    for (plUInt32 i = 1; i <= uiSteps; ++i)
    {
      const float t = static_cast<float>(i) / uiSteps;
      const float u = 1.0f - t;
      const plVec2 p = vP0 * (u * u * u) + vP1 * (3.0f * u * u * t) + vP2 * (3.0f * u * t * t) + vP3 * (t * t * t);
      AddPoint(ref_subPath, p, ref_uiTotalPoints);
    }
  }

  void FlattenQuadratic(SubPath& ref_subPath, const plVec2& vP0, const plVec2& vP1, const plVec2& vP2, plUInt32& ref_uiTotalPoints)
  {
    constexpr plUInt32 uiSteps = 12;
    for (plUInt32 i = 1; i <= uiSteps; ++i)
    {
      const float t = static_cast<float>(i) / uiSteps;
      const float u = 1.0f - t;
      const plVec2 p = vP0 * (u * u) + vP1 * (2.0f * u * t) + vP2 * (t * t);
      AddPoint(ref_subPath, p, ref_uiTotalPoints);
    }
  }

  /// SVG endpoint-parameterized elliptical arc, flattened to line segments.
  void FlattenArc(SubPath& ref_subPath, const plVec2& vFrom, float fRx, float fRy, float fXRotDeg, bool bLargeArc, bool bSweep, const plVec2& vTo, plUInt32& ref_uiTotalPoints)
  {
    fRx = plMath::Abs(fRx);
    fRy = plMath::Abs(fRy);

    if (fRx < 0.0001f || fRy < 0.0001f || (vFrom - vTo).GetLengthSquared() < 0.0001f)
    {
      AddPoint(ref_subPath, vTo, ref_uiTotalPoints);
      return;
    }

    const float fPhi = plAngle::MakeFromDegree(fXRotDeg).GetRadian();
    const float fCosPhi = plMath::Cos(plAngle::MakeFromRadian(fPhi));
    const float fSinPhi = plMath::Sin(plAngle::MakeFromRadian(fPhi));

    // to center parameterization (SVG spec appendix)
    const plVec2 vHalf = (vFrom - vTo) * 0.5f;
    const plVec2 vP1(fCosPhi * vHalf.x + fSinPhi * vHalf.y, -fSinPhi * vHalf.x + fCosPhi * vHalf.y);

    float fLambda = (vP1.x * vP1.x) / (fRx * fRx) + (vP1.y * vP1.y) / (fRy * fRy);
    if (fLambda > 1.0f)
    {
      const float fScale = plMath::Sqrt(fLambda);
      fRx *= fScale;
      fRy *= fScale;
    }

    const float fRx2 = fRx * fRx;
    const float fRy2 = fRy * fRy;
    const float fNum = plMath::Max(fRx2 * fRy2 - fRx2 * vP1.y * vP1.y - fRy2 * vP1.x * vP1.x, 0.0f);
    const float fDen = fRx2 * vP1.y * vP1.y + fRy2 * vP1.x * vP1.x;
    float fCoef = fDen > 0.0f ? plMath::Sqrt(fNum / fDen) : 0.0f;
    if (bLargeArc == bSweep)
      fCoef = -fCoef;

    const plVec2 vCp(fCoef * fRx * vP1.y / fRy, -fCoef * fRy * vP1.x / fRx);
    const plVec2 vCenter(
      fCosPhi * vCp.x - fSinPhi * vCp.y + (vFrom.x + vTo.x) * 0.5f,
      fSinPhi * vCp.x + fCosPhi * vCp.y + (vFrom.y + vTo.y) * 0.5f);

    auto angleOf = [&](const plVec2& v) { return plMath::ATan2(v.y, v.x).GetRadian(); };

    const float fStart = angleOf(plVec2((vP1.x - vCp.x) / fRx, (vP1.y - vCp.y) / fRy));
    float fDelta = angleOf(plVec2((-vP1.x - vCp.x) / fRx, (-vP1.y - vCp.y) / fRy)) - fStart;

    if (!bSweep && fDelta > 0.0f)
      fDelta -= 2.0f * plMath::Pi<float>();
    else if (bSweep && fDelta < 0.0f)
      fDelta += 2.0f * plMath::Pi<float>();

    const plUInt32 uiSteps = plMath::Clamp<plUInt32>(static_cast<plUInt32>(plMath::Abs(fDelta) / 0.26f) + 1, 2, 48); // ~15° per step

    for (plUInt32 i = 1; i <= uiSteps; ++i)
    {
      const float fAngle = fStart + fDelta * (static_cast<float>(i) / uiSteps);
      const float fCosA = plMath::Cos(plAngle::MakeFromRadian(fAngle));
      const float fSinA = plMath::Sin(plAngle::MakeFromRadian(fAngle));
      const plVec2 p(
        vCenter.x + fRx * fCosA * fCosPhi - fRy * fSinA * fSinPhi,
        vCenter.y + fRx * fCosA * fSinPhi + fRy * fSinA * fCosPhi);
      AddPoint(ref_subPath, p, ref_uiTotalPoints);
    }
  }

  void ParsePathData(plStringView sData, plDynamicArray<SubPath>& out_subPaths)
  {
    PathParser parser;
    parser.m_szCur = sData.GetStartPointer();
    parser.m_szEnd = sData.GetEndPointer();

    // XAML fill-rule prefix ("F0" / "F1")
    parser.SkipSeparators();
    if (parser.m_szCur + 2 <= parser.m_szEnd && (*parser.m_szCur == 'F' || *parser.m_szCur == 'f'))
      parser.m_szCur += 2;

    plUInt32 uiTotalPoints = 0;
    plVec2 vCurrent = plVec2::MakeZero();
    plVec2 vSubStart = plVec2::MakeZero();
    plVec2 vLastControl = plVec2::MakeZero();
    char cLastCommand = 0;

    SubPath* pCurrent = nullptr;

    auto beginSubPath = [&](const plVec2& vStart)
    {
      pCurrent = &out_subPaths.ExpandAndGetRef();
      AddPoint(*pCurrent, vStart, uiTotalPoints);
      vSubStart = vStart;
    };

    while (!parser.AtEnd())
    {
      char cCommand = *parser.m_szCur;

      if (parser.PeekIsNumber())
      {
        // implicit command repetition; an implicit M continues as L
        if (cLastCommand == 'M')
          cCommand = 'L';
        else if (cLastCommand == 'm')
          cCommand = 'l';
        else
          cCommand = cLastCommand;
      }
      else
      {
        ++parser.m_szCur;
      }

      if (cCommand == 0)
        break;

      const bool bRelative = cCommand >= 'a' && cCommand <= 'z';
      const char cUpper = bRelative ? static_cast<char>(cCommand - 'a' + 'A') : cCommand;
      const plVec2 vBase = bRelative ? vCurrent : plVec2::MakeZero();

      bool bOk = true;

      switch (cUpper)
      {
        case 'M':
        {
          plVec2 p;
          bOk = parser.ReadPoint(p);
          if (bOk)
          {
            vCurrent = vBase + p;
            beginSubPath(vCurrent);
          }
          break;
        }

        case 'L':
        {
          plVec2 p;
          bOk = parser.ReadPoint(p);
          if (bOk && pCurrent != nullptr)
          {
            vCurrent = vBase + p;
            AddPoint(*pCurrent, vCurrent, uiTotalPoints);
          }
          break;
        }

        case 'H':
        {
          float x = 0.0f;
          bOk = parser.ReadFloat(x);
          if (bOk && pCurrent != nullptr)
          {
            vCurrent.x = (bRelative ? vCurrent.x : 0.0f) + x;
            AddPoint(*pCurrent, vCurrent, uiTotalPoints);
          }
          break;
        }

        case 'V':
        {
          float y = 0.0f;
          bOk = parser.ReadFloat(y);
          if (bOk && pCurrent != nullptr)
          {
            vCurrent.y = (bRelative ? vCurrent.y : 0.0f) + y;
            AddPoint(*pCurrent, vCurrent, uiTotalPoints);
          }
          break;
        }

        case 'C':
        {
          plVec2 c1, c2, p;
          bOk = parser.ReadPoint(c1) && parser.ReadPoint(c2) && parser.ReadPoint(p);
          if (bOk && pCurrent != nullptr)
          {
            FlattenCubic(*pCurrent, vCurrent, vBase + c1, vBase + c2, vBase + p, uiTotalPoints);
            vLastControl = vBase + c2;
            vCurrent = vBase + p;
          }
          break;
        }

        case 'S':
        {
          plVec2 c2, p;
          bOk = parser.ReadPoint(c2) && parser.ReadPoint(p);
          if (bOk && pCurrent != nullptr)
          {
            const char cPrev = cLastCommand >= 'a' ? static_cast<char>(cLastCommand - 'a' + 'A') : cLastCommand;
            const plVec2 c1 = (cPrev == 'C' || cPrev == 'S') ? vCurrent * 2.0f - vLastControl : vCurrent;
            FlattenCubic(*pCurrent, vCurrent, c1, vBase + c2, vBase + p, uiTotalPoints);
            vLastControl = vBase + c2;
            vCurrent = vBase + p;
          }
          break;
        }

        case 'Q':
        {
          plVec2 c1, p;
          bOk = parser.ReadPoint(c1) && parser.ReadPoint(p);
          if (bOk && pCurrent != nullptr)
          {
            FlattenQuadratic(*pCurrent, vCurrent, vBase + c1, vBase + p, uiTotalPoints);
            vLastControl = vBase + c1;
            vCurrent = vBase + p;
          }
          break;
        }

        case 'T':
        {
          plVec2 p;
          bOk = parser.ReadPoint(p);
          if (bOk && pCurrent != nullptr)
          {
            const char cPrev = cLastCommand >= 'a' ? static_cast<char>(cLastCommand - 'a' + 'A') : cLastCommand;
            const plVec2 c1 = (cPrev == 'Q' || cPrev == 'T') ? vCurrent * 2.0f - vLastControl : vCurrent;
            FlattenQuadratic(*pCurrent, vCurrent, c1, vBase + p, uiTotalPoints);
            vLastControl = c1;
            vCurrent = vBase + p;
          }
          break;
        }

        case 'A':
        {
          float fRx = 0, fRy = 0, fRot = 0, fLarge = 0, fSweep = 0;
          plVec2 p;
          bOk = parser.ReadFloat(fRx) && parser.ReadFloat(fRy) && parser.ReadFloat(fRot) && parser.ReadFloat(fLarge) && parser.ReadFloat(fSweep) && parser.ReadPoint(p);
          if (bOk && pCurrent != nullptr)
          {
            const plVec2 vTo = vBase + p;
            FlattenArc(*pCurrent, vCurrent, fRx, fRy, fRot, fLarge != 0.0f, fSweep != 0.0f, vTo, uiTotalPoints);
            vCurrent = vTo;
          }
          break;
        }

        case 'Z':
          vCurrent = vSubStart;
          pCurrent = nullptr;
          break;

        default:
          bOk = false; // unknown command: abort parsing (partial shape is better than garbage)
          break;
      }

      if (!bOk)
        break;

      cLastCommand = cCommand;

      if (uiTotalPoints >= s_uiMaxPathPoints)
        break;
    }

    // drop degenerate subpaths
    for (plUInt32 i = out_subPaths.GetCount(); i-- > 0;)
    {
      // a closing point identical to the start is redundant
      SubPath& subPath = out_subPaths[i];
      if (subPath.GetCount() >= 2 && (subPath.PeekBack() - subPath[0]).GetLengthSquared() < 0.0001f)
        subPath.PopBack();

      if (subPath.GetCount() < 3)
        out_subPaths.RemoveAtAndSwap(i);
    }
  }

  // ————— ear-clipping triangulation —————

  float SignedArea(const SubPath& polygon)
  {
    float fArea = 0.0f;
    for (plUInt32 i = 0; i < polygon.GetCount(); ++i)
    {
      const plVec2& a = polygon[i];
      const plVec2& b = polygon[(i + 1) % polygon.GetCount()];
      fArea += a.x * b.y - b.x * a.y;
    }
    return fArea * 0.5f;
  }

  float Cross2(const plVec2& vO, const plVec2& vA, const plVec2& vB)
  {
    return (vA.x - vO.x) * (vB.y - vO.y) - (vA.y - vO.y) * (vB.x - vO.x);
  }

  bool PointInTriangle(const plVec2& p, const plVec2& a, const plVec2& b, const plVec2& c)
  {
    const float d1 = Cross2(p, a, b);
    const float d2 = Cross2(p, b, c);
    const float d3 = Cross2(p, c, a);

    const bool bHasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool bHasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(bHasNeg && bHasPos);
  }

  void Triangulate(const SubPath& polygon, plDynamicArray<plVec2>& out_triangles)
  {
    const plUInt32 uiCount = polygon.GetCount();
    if (uiCount < 3)
      return;

    const float fOrientation = SignedArea(polygon) >= 0.0f ? 1.0f : -1.0f;

    plHybridArray<plUInt32, 64> indices;
    indices.SetCountUninitialized(uiCount);
    for (plUInt32 i = 0; i < uiCount; ++i)
      indices[i] = i;

    plUInt32 uiGuard = 0;

    while (indices.GetCount() > 3 && uiGuard < uiCount * uiCount)
    {
      bool bClipped = false;

      for (plUInt32 i = 0; i < indices.GetCount(); ++i)
      {
        const plUInt32 uiPrev = indices[(i + indices.GetCount() - 1) % indices.GetCount()];
        const plUInt32 uiCurr = indices[i];
        const plUInt32 uiNext = indices[(i + 1) % indices.GetCount()];

        const plVec2& a = polygon[uiPrev];
        const plVec2& b = polygon[uiCurr];
        const plVec2& c = polygon[uiNext];

        // convex (respecting winding)?
        if (Cross2(a, b, c) * fOrientation <= 0.00001f)
          continue;

        // no other vertex inside?
        bool bContains = false;
        for (plUInt32 j = 0; j < indices.GetCount(); ++j)
        {
          const plUInt32 uiTest = indices[j];
          if (uiTest == uiPrev || uiTest == uiCurr || uiTest == uiNext)
            continue;
          if (PointInTriangle(polygon[uiTest], a, b, c))
          {
            bContains = true;
            break;
          }
        }

        if (bContains)
          continue;

        out_triangles.PushBack(a);
        out_triangles.PushBack(b);
        out_triangles.PushBack(c);
        indices.RemoveAtAndCopy(i);
        bClipped = true;
        break;
      }

      ++uiGuard;

      if (!bClipped)
        break; // degenerate (self-intersecting) remainder: stop cleanly
    }

    if (indices.GetCount() == 3)
    {
      out_triangles.PushBack(polygon[indices[0]]);
      out_triangles.PushBack(polygon[indices[1]]);
      out_triangles.PushBack(polygon[indices[2]]);
    }
  }

  plMutex s_PathTessellationMutex;
} // namespace

const plDynamicArray<plVec2>& plUiPath::GetTriangles(plVec2& out_vMin, plVec2& out_vMax) const
{
  PL_LOCK(s_PathTessellationMutex);

  if (!m_bTessellated)
  {
    m_Triangles.Clear();
    m_vPathMin = plVec2::MakeZero();
    m_vPathMax = plVec2::MakeZero();

    plDynamicArray<SubPath> subPaths;
    ParsePathData(m_sPathData.GetView(), subPaths);

    for (const SubPath& subPath : subPaths)
    {
      Triangulate(subPath, m_Triangles);
    }

    if (!m_Triangles.IsEmpty())
    {
      m_vPathMin = m_vPathMax = m_Triangles[0];
      for (const plVec2& p : m_Triangles)
      {
        m_vPathMin = m_vPathMin.CompMin(p);
        m_vPathMax = m_vPathMax.CompMax(p);
      }
    }

    m_bTessellated = true;
  }

  out_vMin = m_vPathMin;
  out_vMax = m_vPathMax;
  return m_Triangles;
}
