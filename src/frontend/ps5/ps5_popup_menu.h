// SPDX-License-Identifier: GPL-3.0-or-later
// Native UI overlay for the WebKit PlayStation popup menu proxy.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>
#include <WebKit/WKBase.h>

#include "prosperoweb/core/gamepad.h"
#include "prosperoweb/core/touchpad_model.h"
#include "damage_region.h"
#include "splash.h"

namespace prosperoweb::frontend {

class Ps5PopupMenu {
public:
    // Called once per UI frame; page is the active content view, not chrome.
    void update(WKPageRef page, int contentTop, int width, int height,
        display::Region& damage, const std::string& fontFile);
    bool visible() const { return visible_; }
    bool consumeButton(core::Button button, bool pressed, display::Region& damage);
    bool consumeKey(int code, bool pressed, display::Region& damage);
    void scrollStick(float vertical, std::uint64_t now, display::Region& damage);
    bool consumeTouchpad(std::span<const core::TouchContact> contacts, std::uint64_t now,
        display::Region& damage);
    void draw(std::uint32_t* pixels, int stride, SDL_PixelFormat* format,
        const display::Rect& clip) const;

private:
    struct Item {
        std::string label;
        display::GrayImage glyphs;
        bool selectable = false;
    };
    void move(int direction, display::Region& damage);
    void finish(bool accept, display::Region& damage);
    void adjustScroll();

    WKPageRef page_ = nullptr;
    bool visible_ = false;
    bool swallowCrossUp_ = false;
    bool swallowCircleUp_ = false;
    bool swallowTouchpadUp_ = false;
    bool swallowReturnUp_ = false;
    bool swallowEscapeUp_ = false;
    std::uint64_t lastStickMoveMs_ = 0;
    float touchScrollPixels_ = 0.0f;
    core::TouchpadModel menuTouchpad_;
    int initialSelected_ = -1;
    int selected_ = -1;
    int scroll_ = 0;
    int visibleRows_ = 0;
    int rowHeight_ = 36;
    display::Rect rect_;
    std::vector<Item> items_;
};

} // namespace prosperoweb::frontend
