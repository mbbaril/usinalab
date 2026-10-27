#pragma once

// WHERE A PLUGIN WINDOW OPENS, AND AT WHAT SIZE (2026-09-22).
//
// User, playing on the notebook: "todos os plugins precisam ter janela com
// resize, ficou ruim de usar no notebook". Every product in this family
// was ALREADY resizable - each one calls setResizable(true, true). Two
// other things were wrong, and both of them are about the size the window
// OPENS at:
//
//   1. Several open bigger than a notebook screen. The Volca Kick opened
//      at 1100x900; on a 1920x1080 panel at 150% scaling the usable area
//      is about 1280x680, so the window opened taller than the desktop and
//      its bottom edge - with the resize corner on it - was unreachable.
//   2. Nothing remembered the size. Resizing it down was a chore repeated
//      on every single open, in every session, forever.
//
// The Volca Drum already carries a warning about the naive fix: opening at
// the MAXIMUM resizable size was tried there on 2026-08-20 and rejected
// ("janela ficou enorme"). So this is deliberately not that. It opens at
// the size the user last chose, or the product's own preferred size, and
// in either case never bigger than the display it is opening on.
//
// The size lives in a small file per product under the user's application
// data, NOT in the plugin's saved state. That is a deliberate choice:
//   - a window size is a fact about the SCREEN, not about the music, so it
//     should follow the machine rather than travel inside a session that
//     may be opened on a different one;
//   - it needs no format version bump in seven products, and this family
//     has already learned what new file magics cost ([[family-unification]]);
//   - and Debug and Release carry different VST3 UIDs, so a machine-level
//     preference is shared between them where session state is not.

#include <juce_gui_basics/juce_gui_basics.h>

namespace ruidocore::ui {

class EditorWindowSize {
public:
    // `productId` becomes a filename, so keep it short and stable -
    // "volca-drum", "percutere". Changing it forgets the saved size.
    explicit EditorWindowSize(juce::String productIdIn) : productId(std::move(productIdIn)) {}

    // Call from the editor's constructor, INSTEAD of a bare setSize:
    //     const auto size = windowSize.chooseOpeningSize(1150, 700, 900, 560);
    //     setSize(size.getWidth(), size.getHeight());
    juce::Rectangle<int> chooseOpeningSize(int preferredWidth, int preferredHeight, int minimumWidth,
                                            int minimumHeight) const {
        int width = preferredWidth;
        int height = preferredHeight;
        readSaved(width, height);

        const auto room = availableArea();
        // jmax(minimum, ...) last: a window smaller than its own minimum is
        // snapped back up by the constrainer anyway, so clamping below the
        // minimum would only produce a size that silently does not apply.
        // If a product's minimum genuinely does not fit the screen, that is
        // the MINIMUM's bug and it has to come down - this cannot paper
        // over it, and should not pretend to.
        width = juce::jmax(minimumWidth, juce::jmin(width, room.getWidth()));
        height = juce::jmax(minimumHeight, juce::jmin(height, room.getHeight()));
        return {width, height};
    }

    // Call from the editor's DESTRUCTOR, not from resized(): a drag fires
    // resized() dozens of times and none of those intermediate sizes is a
    // decision. One write, when the window closes at the size the user
    // settled on.
    void remember(int width, int height) const {
        if (width <= 0 || height <= 0)
            return;
        auto file = settingsFile();
        if (!file.getParentDirectory().exists())
            file.getParentDirectory().createDirectory();
        file.replaceWithText(juce::String(width) + " " + juce::String(height));
    }

private:
    // What is actually free on the display this window will appear on -
    // userBounds excludes the taskbar. The margin is host chrome: a plugin
    // window sits inside the host's own frame, with a title bar above it,
    // so the full user area is never really available.
    static juce::Rectangle<int> availableArea() {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display = displays.getPrimaryDisplay();
        if (display == nullptr)
            return {1280, 700}; // a conservative notebook, if the OS will not say
        const auto area = display->userBounds.toNearestInt(); // userArea was deprecated in JUCE 9 - same area, as floats
        return {juce::jmax(640, area.getWidth() - 24), juce::jmax(400, area.getHeight() - 64)};
    }

    juce::File settingsFile() const {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Ruido")
            .getChildFile(productId + "-window.txt");
    }

    void readSaved(int& width, int& height) const {
        const auto file = settingsFile();
        if (!file.existsAsFile())
            return;
        auto parts = juce::StringArray::fromTokens(file.loadFileAsString().trim(), " ", "");
        if (parts.size() != 2)
            return;
        const int savedWidth = parts[0].getIntValue();
        const int savedHeight = parts[1].getIntValue();
        // A saved size is only USED when it is sane. A zero, a negative or
        // an absurd value means the file is corrupt, and the product's own
        // preferred size is the better answer than a window nobody can see.
        if (savedWidth >= 320 && savedHeight >= 240 && savedWidth <= 8192 && savedHeight <= 8192) {
            width = savedWidth;
            height = savedHeight;
        }
    }

    juce::String productId;
};

} // namespace ruidocore::ui
