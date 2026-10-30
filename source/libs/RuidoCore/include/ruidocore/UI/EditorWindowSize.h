#pragma once

// Persist editor size per product and fit it within the usable display area.
//
// User, playing on the notebook: "todos os plugins precisam ter janela com
// resize, ficou ruim de usar no notebook". Every product in this family
// was ALREADY resizable - each one calls setResizable(true, true). Two
// other things were wrong, and both of them are about the size the window
// OPENS at:
//
// Clamp restored dimensions to the usable display bounds, including scale and taskbar.
//
// Do not initialize at maximum resizable size; use restored or preferred dimensions.
//
// Persist editor dimensions in per-product application data, separate from session state.
// Clamp restored size to the current usable display bounds.

#include <juce_gui_basics/juce_gui_basics.h>

namespace ruidocore::ui {

class EditorWindowSize {
public:
    // productId is a stable filename component. Changing it resets saved dimensions.
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

    // Save dimensions when the editor closes, not on every resized() callback.
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
        const auto area = display->userBounds.toNearestInt(); // Use the floating-point usable display bounds.
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

} //
