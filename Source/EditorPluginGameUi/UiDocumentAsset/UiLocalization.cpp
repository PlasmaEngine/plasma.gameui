#include <EditorPluginGameUi/EditorPluginGameUiPCH.h>

#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorPluginGameUi/UiDocumentAsset/UiLocalization.h>
#include <Foundation/IO/FileSystem/FileReader.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/Strings/TranslationLookup.h>
#include <ToolsFoundation/Project/ToolsProject.h>

namespace
{
  struct KnownLanguage
  {
    const char* m_szCode;
    const char* m_szNative; ///< UTF-8
    const char* m_szEnglish;
  };

  const KnownLanguage s_KnownLanguages[] = {
    {"en", "English", "English"},
    {"de", "Deutsch", "German"},
    {"fr", "Fran\xC3\xA7" "ais", "French"},
    {"es", "Espa\xC3\xB1ol", "Spanish"},
    {"it", "Italiano", "Italian"},
    {"pt", "Portugu\xC3\xAAs", "Portuguese"},
    {"pl", "Polski", "Polish"},
    {"nl", "Nederlands", "Dutch"},
    {"ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", "Russian"},
    {"tr", "T\xC3\xBCrk\xC3\xA7" "e", "Turkish"},
    {"ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "Japanese"},
    {"zh", "\xE4\xB8\xAD\xE6\x96\x87", "Chinese"},
    {"ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", "Korean"},
  };

  const KnownLanguage* FindKnownLanguage(plStringView sLanguage)
  {
    // "de-DE" and "de_AT" read as "de"
    plStringView sCode = sLanguage;
    if (const char* szSeparator = sLanguage.FindSubString("-"))
      sCode = plStringView(sLanguage.GetStartPointer(), szSeparator);
    else if (const char* szUnderscore = sLanguage.FindSubString("_"))
      sCode = plStringView(sLanguage.GetStartPointer(), szUnderscore);

    for (const KnownLanguage& known : s_KnownLanguages)
    {
      if (sCode.IsEqual_NoCase(known.m_szCode))
        return &known;
    }

    return nullptr;
  }

  void AddFolder(plDynamicArray<plGameUiLocalization::Language>& ref_languages, const plString& sName, const plString& sFolder)
  {
    for (plGameUiLocalization::Language& language : ref_languages)
    {
      if (language.m_sName.IsEqual_NoCase(sName))
      {
        language.m_Folders.PushBack(sFolder);
        return;
      }
    }

    plGameUiLocalization::Language& language = ref_languages.ExpandAndGetRef();
    language.m_sName = sName;
    language.m_Folders.PushBack(sFolder);
  }
} // namespace

namespace plGameUiLocalization
{
  void CollectLanguages(plDynamicArray<Language>& out_languages)
  {
    out_languages.Clear();

    if (!plToolsProject::IsProjectOpen())
      return;

    const plString sProjectDir = plToolsProject::GetSingleton()->GetProjectDirectory();

    for (const char* szRoot : {"Localization", "Editor/Localization"})
    {
      plStringBuilder sRoot(sProjectDir);
      sRoot.AppendPath(szRoot);

      plFileSystemIterator it;
      for (it.StartSearch(sRoot, plFileSystemIteratorFlags::ReportFolders); it.IsValid(); it.Next())
      {
        plStringBuilder sFolder;
        it.GetStats().GetFullPath(sFolder);
        AddFolder(out_languages, it.GetStats().m_sName, sFolder);
      }
    }

    out_languages.Sort([](const Language& a, const Language& b)
      { return a.m_sName.Compare_NoCase(b.m_sName) < 0; });
  }

  const Language* FindLanguage(plArrayPtr<const Language> languages, plStringView sName)
  {
    for (const Language& language : languages)
    {
      if (language.m_sName.IsEqual_NoCase(sName))
        return &language;
    }

    return nullptr;
  }

  plStringView GetNativeName(plStringView sLanguage)
  {
    const KnownLanguage* pKnown = FindKnownLanguage(sLanguage);
    return pKnown != nullptr ? plStringView(pKnown->m_szNative) : sLanguage;
  }

  plStringView GetEnglishName(plStringView sLanguage)
  {
    const KnownLanguage* pKnown = FindKnownLanguage(sLanguage);
    return pKnown != nullptr ? plStringView(pKnown->m_szEnglish) : sLanguage;
  }

  void CollectTableFiles(const Language& language, plDynamicArray<plString>& out_files)
  {
    out_files.Clear();

    // plTranslatorFromFiles reads every file below the folder, so these are the same files
    for (const plString& sFolder : language.m_Folders)
    {
      plFileSystemIterator it;
      for (it.StartSearch(sFolder, plFileSystemIteratorFlags::ReportFilesRecursive); it.IsValid(); it.Next())
      {
        plStringBuilder sFile;
        it.GetStats().GetFullPath(sFile);
        out_files.PushBack(sFile);
      }
    }

    out_files.Sort();
  }

  void AddTableDependencies(plSet<plString>& inout_dependencies)
  {
    plDynamicArray<Language> languages;
    CollectLanguages(languages);

    plDynamicArray<plString> files;
    for (const Language& language : languages)
    {
      CollectTableFiles(language, files);

      for (plString& sFile : files)
      {
        if (plEditorApp::GetSingleton()->MakePathDataDirectoryRelative(sFile))
          inout_dependencies.Insert(sFile);
      }
    }
  }

  void CollectCharacters(const Language& language, plHashSet<plUInt32>& inout_characters)
  {
    plDynamicArray<plString> files;
    CollectTableFiles(language, files);

    for (const plString& sFile : files)
    {
      plFileReader file;
      if (file.Open(sFile).Failed())
        continue;

      plStringBuilder sContent;
      sContent.ReadAll(file);

      plDynamicArray<plStringView> lines;
      sContent.Split(false, lines, "\n");

      for (plStringView sLine : lines)
      {
        sLine.Trim(" \t\r");
        if (sLine.IsEmpty() || sLine.StartsWith("#"))
          continue;

        // "key;text[;tooltip[;help url]]": the text
        const char* szSeparator = sLine.FindSubString(";");
        if (szSeparator == nullptr)
          continue;

        plStringView sText(szSeparator + 1, sLine.GetEndPointer());
        if (const char* szNext = sText.FindSubString(";"))
          sText = plStringView(sText.GetStartPointer(), szNext);

        for (auto it = sText.GetIteratorFront(); it.IsValid(); ++it)
        {
          if (it.GetCharacter() >= 32)
            inout_characters.Insert(it.GetCharacter());
        }
      }
    }
  }

  plUniquePtr<plTranslatorFromFiles> CreateTranslator(const Language& language)
  {
    plUniquePtr<plTranslatorFromFiles> pTranslator = PL_DEFAULT_NEW(plTranslatorFromFiles);

    plStringBuilder sFolder;
    for (const plString& sPath : language.m_Folders)
    {
      sFolder = sPath;
      pTranslator->AddTranslationFilesFromFolder(sFolder);
    }

    return pTranslator;
  }
} // namespace plGameUiLocalization
