#pragma once
// Platform-neutral tab geometry for the Vibrium tab strip.
// Tabs are slanted trapezoids that alternate orientation: even tabs are wide
// at the bottom, odd tabs are wide at the top, so each tab fills the triangular
// gap left by its neighbour.
#include <cmath>

namespace vibrium::tabgeo {

struct Pt { float x; float y; };

struct TabShape {
    Pt poly[4];      // fill polygon, clockwise
    float left;      // bounding box
    float right;
    bool flipped;
};

constexpr float kSlantTan = 0.6745f;  // tan(34 degrees)

inline int Slant(int barHeight) {
    return static_cast<int>(barHeight * kSlantTan + 0.5f);
}
inline bool IsFlipped(int index) { return (index & 1) != 0; }

// Horizontal distance between the left edges of two neighbouring tabs.
inline int Pitch(int width, int slant, int gap) { return width - slant + gap; }

inline int TabLeft(int index, int start, int width, int slant, int gap) {
    return start + index * Pitch(width, slant, gap);
}

// Widest tab width (clamped) so that `count` tabs fit in `available` pixels.
inline int FitWidth(int available, int count, int slant, int gap,
                    int minWidth, int maxWidth) {
    if (count < 1) return minWidth;
    int w = (available + (count - 1) * (slant - gap)) / count;
    return w < minWidth ? minWidth : (w > maxWidth ? maxWidth : w);
}

inline TabShape MakeShape(int left, int width, int h, int s, bool flipped) {
    const float L = static_cast<float>(left), R = static_cast<float>(left + width);
    const float H = static_cast<float>(h), S = static_cast<float>(s);
    TabShape t{};
    t.left = L; t.right = R; t.flipped = flipped;
    if (!flipped) {  // wide at the bottom
        t.poly[0] = {L, H}; t.poly[1] = {L + S, 0}; t.poly[2] = {R - S, 0}; t.poly[3] = {R, H};
    } else {         // wide at the top
        t.poly[0] = {L + S, H}; t.poly[1] = {L, 0}; t.poly[2] = {R, 0}; t.poly[3] = {R - S, H};
    }
    return t;
}

// How far the slanted side is pulled in from the bounding box at height y.
inline float EdgeOffset(const TabShape& t, float y, int h, int s) {
    const float f = y / static_cast<float>(h);
    return static_cast<float>(s) * (t.flipped ? f : 1.0f - f);
}

inline bool Contains(const TabShape& t, float x, float y, int h, int s) {
    if (y < 0 || y >= static_cast<float>(h)) return false;
    const float off = EdgeOffset(t, y, h, s);
    return x >= t.left + off && x <= t.right - off;
}

// Horizontal layout of the whole strip: tab width, spacing, and where the "+"
// button sits. The "+" follows the last tab (like Chrome) until the tabs have
// used all the room, then stays pinned just left of the window controls while
// the tabs shrink (and finally overlap) so nothing runs under the controls.
struct Layout {
    int width;     // width of each tab's bounding box
    int pitch;     // distance between left edges of neighbouring tabs
    int plusLeft;  // left edge of the "+" button
};

inline Layout ComputeLayout(int clientWidth, int barHeight, int count,
                            int start, int gap, int plusSize, int plusMargin,
                            int captionWidth, int captionGap, int maxWidth) {
    const int s = Slant(barHeight);
    const int pinnedPlusLeft = clientWidth - captionWidth - captionGap - plusSize;
    const int avail = pinnedPlusLeft - plusMargin - start;
    Layout l{2 * s + 8, 0, start};
    if (count < 1) return l;
    l.width = FitWidth(avail, count, s, gap, 2 * s + 8, maxWidth);
    l.pitch = Pitch(l.width, s, gap);
    if (count > 1 && l.width + (count - 1) * l.pitch > avail) {
        l.pitch = (avail - l.width) / (count - 1);   // overlap evenly
        if (l.pitch < 1) l.pitch = 1;
    }
    const int lastRight = start + (count - 1) * l.pitch + l.width;
    const int natural = lastRight + plusMargin;
    l.plusLeft = natural < pinnedPlusLeft ? natural : pinnedPlusLeft;
    return l;
}

}  // namespace vibrium::tabgeo
