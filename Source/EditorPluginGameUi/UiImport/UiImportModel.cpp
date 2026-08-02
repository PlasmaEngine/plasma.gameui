#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorPluginGameUi/UiImport/UiImportModel.h>

#include <Foundation/Strings/StringBuilder.h>

namespace
{
  struct NamedColor
  {
    const char* m_szName;
    plUInt32 m_uiRgb;
  };

  // shared web/XAML color names (the common subset; exotic names report as unsupported)
  const NamedColor s_NamedColors[] = {
    {"black", 0x000000}, {"silver", 0xC0C0C0}, {"gray", 0x808080}, {"grey", 0x808080},
    {"white", 0xFFFFFF}, {"maroon", 0x800000}, {"red", 0xFF0000}, {"purple", 0x800080},
    {"fuchsia", 0xFF00FF}, {"magenta", 0xFF00FF}, {"green", 0x008000}, {"lime", 0x00FF00},
    {"olive", 0x808000}, {"yellow", 0xFFFF00}, {"navy", 0x000080}, {"blue", 0x0000FF},
    {"teal", 0x008080}, {"aqua", 0x00FFFF}, {"cyan", 0x00FFFF}, {"orange", 0xFFA500},
    {"gold", 0xFFD700}, {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"beige", 0xF5F5DC},
    {"ivory", 0xFFFFF0}, {"khaki", 0xF0E68C}, {"lavender", 0xE6E6FA}, {"salmon", 0xFA8072},
    {"tomato", 0xFF6347}, {"coral", 0xFF7F50}, {"crimson", 0xDC143C}, {"indigo", 0x4B0082},
    {"violet", 0xEE82EE}, {"orchid", 0xDA70D6}, {"turquoise", 0x40E0D0}, {"skyblue", 0x87CEEB},
    {"steelblue", 0x4682B4}, {"royalblue", 0x4169E1}, {"dodgerblue", 0x1E90FF},
    {"cornflowerblue", 0x6495ED}, {"slategray", 0x708090}, {"darkgray", 0xA9A9A9},
    {"darkgrey", 0xA9A9A9}, {"dimgray", 0x696969}, {"lightgray", 0xD3D3D3},
    {"lightgrey", 0xD3D3D3}, {"gainsboro", 0xDCDCDC}, {"whitesmoke", 0xF5F5F5},
    {"darkred", 0x8B0000}, {"darkgreen", 0x006400}, {"darkblue", 0x00008B},
    {"darkorange", 0xFF8C00}, {"forestgreen", 0x228B22}, {"seagreen", 0x2E8B57},
    {"limegreen", 0x32CD32}, {"goldenrod", 0xDAA520}, {"chocolate", 0xD2691E},
    {"firebrick", 0xB22222}, {"midnightblue", 0x191970}, {"transparent", 0x000000}};

  plUInt8 HexNibble(char c)
  {
    if (c >= '0' && c <= '9')
      return static_cast<plUInt8>(c - '0');
    if (c >= 'a' && c <= 'f')
      return static_cast<plUInt8>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
      return static_cast<plUInt8>(c - 'A' + 10);
    return 0;
  }

  bool IsHexString(plStringView s)
  {
    for (plUInt32 i = 0; i < s.GetElementCount(); ++i)
    {
      const char c = s.GetStartPointer()[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        return false;
    }
    return true;
  }
} // namespace

plVec4 plUiImportUtils::ComputePlacement(const plRectFloat& childRect, const plRectFloat& parentRect, const plVec2& vAnchorMin, const plVec2& vAnchorMax)
{
  plVec4 vOffsets = plVec4::MakeZero();

  // X axis
  {
    const float fAnchorMinPos = parentRect.x + vAnchorMin.x * parentRect.width;
    vOffsets.x = childRect.x - fAnchorMinPos;

    if (vAnchorMin.x == vAnchorMax.x)
    {
      vOffsets.z = childRect.width;
    }
    else
    {
      const float fAnchorMaxPos = parentRect.x + vAnchorMax.x * parentRect.width;
      vOffsets.z = (childRect.x + childRect.width) - fAnchorMaxPos;
    }
  }

  // Y axis
  {
    const float fAnchorMinPos = parentRect.y + vAnchorMin.y * parentRect.height;
    vOffsets.y = childRect.y - fAnchorMinPos;

    if (vAnchorMin.y == vAnchorMax.y)
    {
      vOffsets.w = childRect.height;
    }
    else
    {
      const float fAnchorMaxPos = parentRect.y + vAnchorMax.y * parentRect.height;
      vOffsets.w = (childRect.y + childRect.height) - fAnchorMaxPos;
    }
  }

  return vOffsets;
}

plResult plUiImportUtils::ParseColor(plStringView sValue, bool bAlphaFirst, plColorGammaUB& out_color)
{
  plStringBuilder sVal = sValue;
  sVal.Trim(" \t\r\n");

  if (sVal.IsEmpty())
    return PL_FAILURE;

  if (sVal.StartsWith("#"))
  {
    plStringView sHex = sVal.GetView().GetShrunk(1);
    if (!IsHexString(sHex))
      return PL_FAILURE;

    const char* szHex = sHex.GetStartPointer();
    const plUInt32 uiLen = sHex.GetElementCount();

    auto byteAt = [&](plUInt32 i) { return static_cast<plUInt8>((HexNibble(szHex[i * 2]) << 4) | HexNibble(szHex[i * 2 + 1])); };

    if (uiLen == 3) // #rgb
    {
      out_color = plColorGammaUB(HexNibble(szHex[0]) * 17, HexNibble(szHex[1]) * 17, HexNibble(szHex[2]) * 17, 255);
      return PL_SUCCESS;
    }
    if (uiLen == 6) // #rrggbb
    {
      out_color = plColorGammaUB(byteAt(0), byteAt(1), byteAt(2), 255);
      return PL_SUCCESS;
    }
    if (uiLen == 8) // #rrggbbaa (CSS) or #aarrggbb (XAML)
    {
      if (bAlphaFirst)
        out_color = plColorGammaUB(byteAt(1), byteAt(2), byteAt(3), byteAt(0));
      else
        out_color = plColorGammaUB(byteAt(0), byteAt(1), byteAt(2), byteAt(3));
      return PL_SUCCESS;
    }

    return PL_FAILURE;
  }

  if (sVal.StartsWith_NoCase("rgb"))
  {
    const char* szOpen = sVal.FindSubString("(");
    const char* szClose = sVal.FindLastSubString(")");
    if (szOpen == nullptr || szClose == nullptr || szClose <= szOpen)
      return PL_FAILURE;

    plStringBuilder sArgs;
    sArgs.SetSubString_FromTo(szOpen + 1, szClose);

    plHybridArray<plStringView, 4> parts;
    sArgs.Split(false, parts, ",", "/", " ");

    if (parts.GetCount() < 3)
      return PL_FAILURE;

    double fVals[4] = {0, 0, 0, 255};
    for (plUInt32 i = 0; i < parts.GetCount() && i < 4; ++i)
    {
      plStringBuilder sPart = parts[i];
      sPart.Trim(" \t");
      const bool bPercent = sPart.EndsWith("%");
      sPart.TrimRight("%");

      double fVal = 0.0;
      if (plConversionUtils::StringToFloat(sPart, fVal).Failed())
        return PL_FAILURE;

      if (bPercent)
        fVal = fVal * 255.0 / 100.0;
      else if (i == 3 && fVal <= 1.0) // rgba() alpha is 0..1
        fVal = fVal * 255.0;

      fVals[i] = fVal;
    }

    out_color = plColorGammaUB(
      static_cast<plUInt8>(plMath::Clamp(fVals[0], 0.0, 255.0)),
      static_cast<plUInt8>(plMath::Clamp(fVals[1], 0.0, 255.0)),
      static_cast<plUInt8>(plMath::Clamp(fVals[2], 0.0, 255.0)),
      static_cast<plUInt8>(plMath::Clamp(fVals[3], 0.0, 255.0)));
    return PL_SUCCESS;
  }

  for (const NamedColor& named : s_NamedColors)
  {
    if (sVal.IsEqual_NoCase(named.m_szName))
    {
      const plUInt8 uiAlpha = sVal.IsEqual_NoCase("transparent") ? 0 : 255;
      out_color = plColorGammaUB((named.m_uiRgb >> 16) & 0xFF, (named.m_uiRgb >> 8) & 0xFF, named.m_uiRgb & 0xFF, uiAlpha);
      return PL_SUCCESS;
    }
  }

  return PL_FAILURE;
}

void plUiImportUtils::DecodeEntities(plStringView sText, plStringBuilder& out_sDecoded)
{
  out_sDecoded.Clear();

  const char* szCur = sText.GetStartPointer();
  const char* szEnd = sText.GetEndPointer();

  while (szCur < szEnd)
  {
    if (*szCur != '&')
    {
      out_sDecoded.Append(plStringView(szCur, szCur + 1));
      ++szCur;
      continue;
    }

    // find the terminating ';' within a reasonable distance
    const char* szSemi = nullptr;
    for (const char* s = szCur + 1; s < szEnd && s < szCur + 12; ++s)
    {
      if (*s == ';')
      {
        szSemi = s;
        break;
      }
    }

    if (szSemi == nullptr)
    {
      out_sDecoded.Append(plStringView(szCur, szCur + 1));
      ++szCur;
      continue;
    }

    plStringView sEntity(szCur + 1, szSemi);

    if (sEntity == "amp")
      out_sDecoded.Append("&");
    else if (sEntity == "lt")
      out_sDecoded.Append("<");
    else if (sEntity == "gt")
      out_sDecoded.Append(">");
    else if (sEntity == "quot")
      out_sDecoded.Append("\"");
    else if (sEntity == "apos")
      out_sDecoded.Append("'");
    else if (sEntity == "nbsp")
      out_sDecoded.Append(" ");
    else if (sEntity.StartsWith("#"))
    {
      plUInt32 uiCodepoint = 0;
      plStringView sNum = sEntity.GetShrunk(1);

      if (sNum.StartsWith("x") || sNum.StartsWith("X"))
      {
        sNum.Shrink(1, 0);
        for (plUInt32 i = 0; i < sNum.GetElementCount(); ++i)
          uiCodepoint = uiCodepoint * 16 + HexNibble(sNum.GetStartPointer()[i]);
      }
      else
      {
        plInt32 iVal = 0;
        if (plConversionUtils::StringToInt(sNum, iVal).Succeeded())
          uiCodepoint = static_cast<plUInt32>(iVal);
      }

      if (uiCodepoint != 0)
      {
        char utf8[5] = {};
        if (uiCodepoint < 0x80)
        {
          utf8[0] = static_cast<char>(uiCodepoint);
        }
        else if (uiCodepoint < 0x800)
        {
          utf8[0] = static_cast<char>(0xC0 | (uiCodepoint >> 6));
          utf8[1] = static_cast<char>(0x80 | (uiCodepoint & 0x3F));
        }
        else if (uiCodepoint < 0x10000)
        {
          utf8[0] = static_cast<char>(0xE0 | (uiCodepoint >> 12));
          utf8[1] = static_cast<char>(0x80 | ((uiCodepoint >> 6) & 0x3F));
          utf8[2] = static_cast<char>(0x80 | (uiCodepoint & 0x3F));
        }
        else
        {
          utf8[0] = static_cast<char>(0xF0 | (uiCodepoint >> 18));
          utf8[1] = static_cast<char>(0x80 | ((uiCodepoint >> 12) & 0x3F));
          utf8[2] = static_cast<char>(0x80 | ((uiCodepoint >> 6) & 0x3F));
          utf8[3] = static_cast<char>(0x80 | (uiCodepoint & 0x3F));
        }
        out_sDecoded.Append(utf8);
      }
    }
    else
    {
      // unknown entity: keep verbatim
      out_sDecoded.Append(plStringView(szCur, szSemi + 1));
    }

    szCur = szSemi + 1;
  }
}
