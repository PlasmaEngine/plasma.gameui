#pragma once

#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Math/Math.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Strings/StringConversion.h>
#include <Foundation/Utilities/ConversionUtils.h>

/// \brief Parses a XAML KeyTime / Duration ("s", "m:s" or "h:m:s") into seconds.
///
/// Anything that is not one to three numeric fields (empty fields, a fourth field, text, overflow) yields 0.
inline float plUiParseXamlKeyTime(plStringView sValue)
{
  plStringBuilder sTime = sValue;
  sTime.Trim(" \t");
  if (sTime.IsEmpty())
    return 0.0f;

  plHybridArray<plStringView, 4> parts;
  sTime.Split(true, parts, ":");

  if (parts.IsEmpty() || parts.GetCount() > 3)
    return 0.0f;

  double values[3] = {0.0, 0.0, 0.0};
  for (plUInt32 i = 0; i < parts.GetCount(); ++i)
  {
    if (plConversionUtils::StringToFloat(parts[i], values[i]).Failed())
      return 0.0f;
  }

  double fSeconds = values[parts.GetCount() - 1];
  if (parts.GetCount() >= 2)
    fSeconds += values[parts.GetCount() - 2] * 60.0;
  if (parts.GetCount() == 3)
    fSeconds += values[0] * 3600.0;

  if (!plMath::IsFinite(fSeconds) || fSeconds > static_cast<double>(plMath::MaxValue<float>()) || fSeconds < static_cast<double>(-plMath::MaxValue<float>()))
    return 0.0f;

  return static_cast<float>(fSeconds);
}
