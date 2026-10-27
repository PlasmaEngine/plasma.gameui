#include <GameUiTest/GameUiTestPCH.h>

#include "../EditorPluginGameUi/UiImport/UiImportKeyTime.h"

PL_CREATE_SIMPLE_TEST_GROUP(GameUiXaml);

PL_CREATE_SIMPLE_TEST(GameUiXaml, KeyTime)
{
  PL_TEST_BLOCK(plTestBlock::Enabled, "Valid forms")
  {
    PL_TEST_FLOAT(plUiParseXamlKeyTime("2.5"), 2.5f, 0.0001f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime(" 1:30 "), 90.0f, 0.0001f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime("1:02:03.5"), 3723.5f, 0.0001f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime("0:0:0"), 0.0f, 0.0001f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Too many components do not write past the value array")
  {
    PL_TEST_FLOAT(plUiParseXamlKeyTime("1:2:3:4"), 0.0f, 0.0f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime("1:2:3:4:5:6:7:8:9"), 0.0f, 0.0f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime(":::::"), 0.0f, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Empty and malformed fields")
  {
    PL_TEST_FLOAT(plUiParseXamlKeyTime(""), 0.0f, 0.0f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime("   "), 0.0f, 0.0f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime("1::3"), 0.0f, 0.0f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime("1:"), 0.0f, 0.0f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime(":5"), 0.0f, 0.0f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime("abc"), 0.0f, 0.0f);
    PL_TEST_FLOAT(plUiParseXamlKeyTime("1:x"), 0.0f, 0.0f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Overflow stays finite")
  {
    const float fValue = plUiParseXamlKeyTime("1e300:1e300:1e300");
    PL_TEST_BOOL(plMath::IsFinite(fValue));
    PL_TEST_FLOAT(plUiParseXamlKeyTime("1e400"), 0.0f, 0.0f);
  }
}
