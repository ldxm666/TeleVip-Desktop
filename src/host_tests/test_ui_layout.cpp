#include "ui_layout.h"

#include <cassert>
#include <iostream>

int main() {
    for (const float height : {760.0f, 800.0f, 860.0f, 960.0f, 1080.0f}) {
        const auto sidebar = tv::sidebar_layout(height);
        const float nav_bottom = sidebar.nav_top + 46 + 9 * sidebar.nav_height;
        assert(nav_bottom + 15.9f <= sidebar.connection_top);
        assert(sidebar.connection_top + 158 + 15.9f <= sidebar.footer_top);
        assert(sidebar.footer_top + 98 <= height);
    }
    for (const float width : {360.0f, 620.0f, 660.0f, 752.0f, 932.0f, 1200.0f}) {
        const auto support = tv::support_layout(width);
        assert(support.qr_size + 48 <= support.card_width + 0.01f);
        assert(support.card_height >= support.qr_size + 154);
        assert(support.columns * support.card_width + (support.columns - 1) * 16 <= width + 0.01f);
        assert(support.columns == (width < 660 ? 1 : 2));
        assert(support.donation_height == (support.columns == 2 ? support.card_height : support.card_height * 2 + 16));
    }
    std::cout << "UI layout: navigation, connection/footer separation, QR margins and responsive columns passed\n";
}
