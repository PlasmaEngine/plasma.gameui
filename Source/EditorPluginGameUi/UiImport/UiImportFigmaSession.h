#pragma once

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/UniquePtr.h>

#include <functional>

/// \brief The Figma REST side of "Import UI from Figma", which the import dialog drives.
///
/// Fetch reads a file's top-level frames with a personal access token. Import downloads the image fills and vector
/// renders of the chosen frames into <folder>/FigmaImages, writes one UI document per frame into the folder, saves and
/// closes them, shows the import report and opens the first document. Both report their progress as the dialog's
/// status text and show the wait cursor while a request runs.
class plUiFigmaRestSession
{
public:
  using StatusCallback = std::function<void(plStringView)>;

  struct Frame
  {
    plString m_sPage;
    plString m_sName;
  };

  plUiFigmaRestSession();
  ~plUiFigmaRestSession();

  /// Fetches the file behind a file URL or a raw key. onStatus receives "Fetching file..." and the result: the number of
  /// frames found, or why nothing was found. Returns true when the file has top-level frames.
  bool Fetch(plStringView sUrlOrKey, plStringView sToken, const StatusCallback& onStatus);

  /// The top-level frames of the last Fetch, page by page; empty after a failed one.
  plArrayPtr<const Frame> GetFrames() const { return m_Frames; }

  /// Imports frames (indices into GetFrames) into sFolder. onStatus receives the steps ("Downloading image fills...",
  /// "Rendering vector shapes...", "Importing frames...").
  void Import(plStringView sUrlOrKey, plStringView sToken, plStringView sFolder, plArrayPtr<const plInt32> frames, const StatusCallback& onStatus);

private:
  struct Data;

  plUniquePtr<Data> m_pData;
  plDynamicArray<Frame> m_Frames;
};
