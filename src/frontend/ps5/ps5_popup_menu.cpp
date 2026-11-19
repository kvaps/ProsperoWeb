// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps5_popup_menu.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <WebKit/WKPagePrivatePlayStation.h>

#include "ps5_log.h"
#include "ps5_splash_text.h"

namespace prosperoweb::frontend {
namespace {
constexpr int kMaxItems = 1024;
constexpr int kMaxVisible = 10;
constexpr int kMargin = 4;
}

void Ps5PopupMenu::adjustScroll()
{
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visibleRows_) scroll_ = selected_ - visibleRows_ + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(items_.size()) - visibleRows_));
}

void Ps5PopupMenu::update(WKPageRef page, int contentTop, int width, int height,
    display::Region& damage, const std::string& fontFile)
{
    int count = 0, selected = -1, x = 0, y = 0, w = 0, h = 0;
    const bool open = page && WKPagePopupMenuInfoPlayStation(page, &count, &selected, &x, &y, &w, &h);
    if (!open || count <= 0 || count > kMaxItems) {
        if (visible_) damage.add(rect_);
        visible_ = false;
        page_ = nullptr;
        items_.clear();
        return;
    }
    // No need to rebuild glyphs while navigating this already-open popup.
    if (visible_ && page_ == page && static_cast<int>(items_.size()) == count
        && initialSelected_ == selected)
        return;
    if (visible_) damage.add(rect_);
    visible_ = true;
    page_ = page;
    initialSelected_ = selected;
    selected_ = selected;
    rowHeight_ = std::max(30, height * 38 / 1080);
    visibleRows_ = std::min({count, kMaxVisible, std::max(1, (height - contentTop - 20) / rowHeight_)});
    int menuWidth = std::min(width, std::max(280, std::min(680, w + 180)));
    int menuHeight = visibleRows_ * rowHeight_ + kMargin * 2;
    rect_ = {std::clamp(x, 0, std::max(0, width - menuWidth)),
        std::clamp(y + h + contentTop, contentTop, std::max(contentTop, height - menuHeight)),
        menuWidth, menuHeight};
    menuTouchpad_.reset();
    touchScrollPixels_ = 0;
    items_.clear();
    items_.reserve(count);
    const int fontSize = std::max(16, rowHeight_ * 24 / 38);
    for (int i = 0; i < count; ++i) {
        char label[2048]{};
        bool selectable = false;
        if (!WKPagePopupMenuItemPlayStation(page, i, label, sizeof(label), &selectable)) {
            label[0] = '\0';
        }
        Item item;
        item.label = label;
        item.selectable = selectable;
        std::string error;
        if (!item.label.empty() && !render_splash_text(fontFile, item.label, fontSize, item.glyphs, error))
            log_printf("popup: text rendering failed: %s", error.c_str());
        items_.push_back(std::move(item));
    }
    if (selected_ < 0 || selected_ >= count || !items_[selected_].selectable) {
        selected_ = -1;
        for (int i = 0; i < count; ++i) if (items_[i].selectable) { selected_ = i; break; }
    }
    scroll_ = 0;
    adjustScroll();
    damage.add(rect_);
    log_printf("popup: open %d items, selection %d at %d,%d", count, selected_, rect_.x, rect_.y);
}

void Ps5PopupMenu::move(int direction, display::Region& damage)
{
    if (items_.empty()) return;
    int i = selected_;
    if (i < 0) i = direction > 0 ? -1 : static_cast<int>(items_.size());
    for (int n = 0; n < static_cast<int>(items_.size()); ++n) {
        i += direction;
        if (i < 0 || i >= static_cast<int>(items_.size())) break;
        if (items_[i].selectable) {
            selected_ = i;
            adjustScroll();
            damage.add(rect_);
            return;
        }
    }
}

void Ps5PopupMenu::scrollStick(float vertical, std::uint64_t now, display::Region& damage)
{
    if (!visible_ || (vertical > -0.55f && vertical < 0.55f)) {
        lastStickMoveMs_ = 0;
        return;
    }
    if (lastStickMoveMs_ && now - lastStickMoveMs_ < 160) return;
    lastStickMoveMs_ = now;
    move(vertical > 0 ? 1 : -1, damage);
}

bool Ps5PopupMenu::consumeTouchpad(std::span<const core::TouchContact> contacts,
    std::uint64_t now, display::Region& damage)
{
    if (!visible_) { menuTouchpad_.reset(); return false; }
    menuTouchpad_.update(contacts, now);
    auto movement = menuTouchpad_.take();
    touchScrollPixels_ += movement.pointer.y + movement.scroll.y;
    while (touchScrollPixels_ >= 65.0f) { move(1, damage); touchScrollPixels_ -= 65.0f; }
    while (touchScrollPixels_ <= -65.0f) { move(-1, damage); touchScrollPixels_ += 65.0f; }
    // A quick touchpad tap confirms the highlighted native option.
    if (movement.clicks) finish(true, damage);
    return true;
}

void Ps5PopupMenu::finish(bool accept, display::Region& damage)
{
    if (!visible_) return;
    if (accept && selected_ >= 0 && items_[selected_].selectable)
        WKPagePopupMenuActivatePlayStation(page_, selected_);
    else
        WKPagePopupMenuCancelPlayStation(page_);
    damage.add(rect_);
    visible_ = false;
    page_ = nullptr;
    items_.clear();
}

bool Ps5PopupMenu::consumeButton(core::Button button, bool pressed, display::Region& damage)
{
    if (!pressed && button == core::Button::Cross && swallowCrossUp_) {
        swallowCrossUp_ = false; return true;
    }
    if (!pressed && button == core::Button::Circle && swallowCircleUp_) {
        swallowCircleUp_ = false; return true;
    }
    if (!pressed && button == core::Button::Touchpad && swallowTouchpadUp_) {
        swallowTouchpadUp_ = false; return true;
    }
    if (!visible_) return false;
    switch (button) {
    case core::Button::Up: if (pressed) move(-1, damage); return true;
    case core::Button::Down: if (pressed) move(1, damage); return true;
    case core::Button::Left: if (pressed) move(-1, damage); return true;
    case core::Button::Right: if (pressed) move(1, damage); return true;
    case core::Button::L1:
        if (pressed) for (int n = 0; n < std::max(1, visibleRows_ - 1); ++n) move(-1, damage);
        return true;
    case core::Button::R1:
        if (pressed) for (int n = 0; n < std::max(1, visibleRows_ - 1); ++n) move(1, damage);
        return true;
    case core::Button::Touchpad:
        if (pressed) { swallowTouchpadUp_ = true; finish(true, damage); }
        return true;
    case core::Button::Cross:
        if (pressed) { swallowCrossUp_ = true; finish(true, damage); return true; }
        // The opening click's mouse-up belongs to ShellController; it must
        // clear the pressed mouse state rather than being lost in the popup.
        return false;
    case core::Button::Circle:
        if (pressed) { swallowCircleUp_ = true; finish(false, damage); }
        return true;
    default: return false;
    }
}

bool Ps5PopupMenu::consumeKey(int code, bool pressed, display::Region& damage)
{
    if (!pressed && code == 13 && swallowReturnUp_) {
        swallowReturnUp_ = false; return true;
    }
    if (!pressed && code == 27 && swallowEscapeUp_) {
        swallowEscapeUp_ = false; return true;
    }
    if (!visible_) return false;
    switch (code) {
    case 38: if (pressed) move(-1, damage); return true;
    case 40: if (pressed) move(1, damage); return true;
    case 33:
        if (pressed) for (int n = 0; n < std::max(1, visibleRows_ - 1); ++n) move(-1, damage);
        return true;
    case 34:
        if (pressed) for (int n = 0; n < std::max(1, visibleRows_ - 1); ++n) move(1, damage);
        return true;
    case 13:
        if (pressed) { swallowReturnUp_ = true; finish(true, damage); return true; }
        return false; // opening key-up must reach WebKit
    case 27:
        if (pressed) { swallowEscapeUp_ = true; finish(false, damage); }
        return true;
    default: return false;
    }
}

void Ps5PopupMenu::draw(std::uint32_t* pixels, int stride, SDL_PixelFormat* format,
    const display::Rect& clip) const
{
    if (!visible_ || !pixels || !format) return;
    const auto r = display::intersect(rect_, clip);
    if (r.empty()) return;
    const auto bg = SDL_MapRGB(format, 25, 29, 37);
    const auto selectedBg = SDL_MapRGB(format, 39, 93, 175);
    const auto edge = SDL_MapRGB(format, 115, 131, 161);
    for (int y = r.y; y < r.bottom(); ++y) {
        auto* dst = pixels + static_cast<size_t>(y) * stride;
        int itemRow = (y - rect_.y - kMargin) / rowHeight_;
        int idx = scroll_ + itemRow;
        bool rowSelected = y >= rect_.y + kMargin && itemRow >= 0 && itemRow < visibleRows_
            && idx == selected_;
        for (int x = r.x; x < r.right(); ++x) {
            bool border = x == rect_.x || x == rect_.right()-1 || y == rect_.y || y == rect_.bottom()-1;
            dst[x] = border ? edge : rowSelected ? selectedBg : bg;
        }
    }
    for (int row = 0; row < visibleRows_; ++row) {
        const int index = scroll_ + row;
        if (index >= static_cast<int>(items_.size())) break;
        const auto& it = items_[index];
        const int tx = rect_.x + 12;
        const int ty = rect_.y + kMargin + row*rowHeight_ + (rowHeight_ - it.glyphs.height)/2;
        const int baseR = index == selected_ ? 39 : 25;
        const int baseG = index == selected_ ? 93 : 29;
        const int baseB = index == selected_ ? 175 : 37;
        const int fgR = it.selectable ? 245 : 148;
        const int fgG = it.selectable ? 248 : 157;
        const int fgB = it.selectable ? 253 : 174;
        std::array<std::uint32_t,256> shades{};
        for (int alpha = 0; alpha < 256; ++alpha) {
            auto blend = [alpha](int lo, int hi) { return (lo*(255-alpha) + hi*alpha + 127)/255; };
            shades[alpha] = SDL_MapRGB(format, blend(baseR,fgR), blend(baseG,fgG), blend(baseB,fgB));
        }
        const int y0 = std::max({ty, clip.y, rect_.y + kMargin});
        const int y1 = std::min({ty + it.glyphs.height, clip.bottom(), rect_.bottom()-kMargin});
        const int x0 = std::max({tx, clip.x, rect_.x + kMargin});
        const int x1 = std::min({tx + it.glyphs.width, clip.right(), rect_.right()-kMargin});
        for (int y = y0; y < y1; ++y) {
            auto* dst = pixels + static_cast<size_t>(y)*stride;
            const auto* src = it.glyphs.coverage.data() + static_cast<size_t>(y-ty)*it.glyphs.width;
            for (int x = x0; x < x1; ++x) if (auto a = src[x-tx]) dst[x] = shades[a];
        }
    }
}

} // namespace prosperoweb::frontend
