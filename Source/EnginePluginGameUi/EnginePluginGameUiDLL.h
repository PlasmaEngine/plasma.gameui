#pragma once

#include <Foundation/Basics.h>
#include <Foundation/Configuration/Plugin.h>

// Configure the DLL Import/Export Define
#if PL_ENABLED(PL_COMPILE_ENGINE_AS_DLL)
#  ifdef BUILDSYSTEM_BUILDING_ENGINEPLUGINGAMEUI_LIB
#    define PL_ENGINEPLUGINGAMEUI_DLL PL_DECL_EXPORT
#  elif defined(BUILDSYSTEM_FOLDING_PLUGIN_IMPORTS)
#    define PL_ENGINEPLUGINGAMEUI_DLL
#  else
#    define PL_ENGINEPLUGINGAMEUI_DLL PL_DECL_IMPORT
#  endif
#else
#  define PL_ENGINEPLUGINGAMEUI_DLL
#endif
