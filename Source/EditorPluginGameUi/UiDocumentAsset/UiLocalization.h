#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/HashSet.h>
#include <Foundation/Containers/Set.h>
#include <Foundation/Strings/String.h>

class plTranslatorFromFiles;

/// \brief The project's string tables, as the Game UI editors use them: the language preview, the Text fit check
/// of the asset transform and the characters a UI Font bakes.
///
/// A language is a subfolder of <project>/Localization or <project>/Editor/Localization ("de"); its tables are the
/// ".txt" files in it, one "key;text" line per string.
namespace plGameUiLocalization
{
  struct Language
  {
    plString m_sName;                 ///< the folder name, "de"
    plDynamicArray<plString> m_Folders; ///< absolute folders holding its tables
  };

  /// Every language of the open project, sorted by name.
  void CollectLanguages(plDynamicArray<Language>& out_languages);

  /// The language of a name, or nullptr.
  const Language* FindLanguage(plArrayPtr<const Language> languages, plStringView sName);

  /// The native name of a language code ("Deutsch" for "de"), or the code when it is not a known one.
  plStringView GetNativeName(plStringView sLanguage);

  /// The English name of a language code ("Japanese" for "ja"), or the code.
  plStringView GetEnglishName(plStringView sLanguage);

  /// The string table files of a language.
  void CollectTableFiles(const Language& language, plDynamicArray<plString>& out_files);

  /// Adds the string table files of every language, data directory relative, as asset dependencies: an edited
  /// translation then re-transforms the asset.
  void AddTableDependencies(plSet<plString>& inout_dependencies);

  /// Adds every character the texts of a language's tables use (not their keys).
  void CollectCharacters(const Language& language, plHashSet<plUInt32>& inout_characters);

  /// A translator with a language's tables loaded; it is not registered with plTranslationLookup.
  plUniquePtr<plTranslatorFromFiles> CreateTranslator(const Language& language);
} // namespace plGameUiLocalization
