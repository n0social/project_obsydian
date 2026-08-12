#pragma once

// The interface's typefaces, by the name FrameXML calls them.
//
// A font object names its face as a path — "Fonts\MORPHEUS.ttf" — and the file
// on disk is lower case, so lookups go through a normalised key rather than the
// spelling either side happens to use. Faces are registered once, before the
// first frame, because the glyph atlas is built then and cannot take another
// afterwards without being torn down.

#include <string>

struct ImFont;

namespace wowee::ui {

/// Records a face under a path or filename. Later registrations of the same
/// face replace the earlier one.
void registerInterfaceFace(const std::string& pathOrName, ImFont* font);

/// The face for a path a font object named, or null if it was never loaded —
/// in which case the caller should draw with whatever it was already using.
ImFont* interfaceFace(const std::string& pathOrName);

} // namespace wowee::ui
