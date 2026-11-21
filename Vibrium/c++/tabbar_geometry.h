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

}  // namespace vibrium::tabgeo
