#pragma once

#include <algorithm>

namespace tv {

struct SidebarLayout {
    float nav_top;
    float nav_height;
    float connection_top;
    float footer_top;
};

inline SidebarLayout sidebar_layout(float height) {
    const float footer = height - 110.0f;
    const float connection = footer - 174.0f;
    const float nav_top = 122.0f;
    // Reserve the connection card and community actions before sizing navigation.
    const float row = std::clamp((connection - 16.0f - nav_top - 46.0f) / 9.0f,
                                 24.0f, 36.0f);
    return {nav_top, row, connection, footer};
}

struct SupportLayout {
    int columns;
    float card_width;
    float qr_size;
    float card_height;
    float donation_height;
};

inline SupportLayout support_layout(float width) {
    constexpr float gap = 16.0f;
    const int columns = width >= 660.0f ? 2 : 1;
    const float card_width = (width - gap * (columns - 1)) / columns;
    const float qr = std::min(264.0f, card_width - 48.0f);
    const float card_height = qr + 154.0f;
    return {columns, card_width, qr, card_height,
            columns == 2 ? card_height : card_height * 2.0f + gap};
}

} // namespace tv
