#include <GameUiTest/GameUiTestPCH.h>

#include <TestFramework/Utilities/TestSetup.h>

// Headless tests of the Game UI runtime: layout, measuring, text fit, pointer rays and the flow states.
// They build element trees and fonts in code, so they need no test data and no GPU.
PL_TESTFRAMEWORK_ENTRY_POINT_BEGIN("GameUiTest", "Game UI Tests")
{
  plTestFramework::GetInstance()->SetTestTimeout(1000 * 60 * 5);
  plTestFramework::s_bCallstackOnAssert = true;
}
PL_TESTFRAMEWORK_ENTRY_POINT_END()
