// The Cocoa side of the accessibility tree (#142): the Metal view answers
// VoiceOver with one NSAccessibilityElement per node of the last frame's
// snapshot, keyed so the same widget is the same element across frames, and
// posts focus, layout and announcement notifications as the UI changes.
#pragma once

#include <string>

#ifdef __OBJC__
#import <MetalKit/MetalKit.h>
@interface OWUIMetalView : MTKView
@end
#endif

namespace a11y::bridge {

// After each frame: notifications, element cache upkeep, the debug channel.
void frameDone(void* metalView);

// The tree as an assistive app reads it: walks the window through the public
// NSAccessibility methods (role, label, value, frame, children) - the same
// calls VoiceOver's server makes, minus the IPC.
std::string dumpWindow(void* metalView, int maxNodes = 4000);

// Finds the first element whose label or title is `label` and presses it
// (accessibilityPerformPress) or focuses it. False when none matched.
bool pressLabel(void* metalView, const std::string& label);
bool focusLabel(void* metalView, const std::string& label);

// What the window reports as focused, described like a dump line.
std::string describeFocused(void* metalView);

}  // namespace a11y::bridge
