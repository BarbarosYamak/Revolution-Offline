#include "render/Text.h"

#include "render/Renderer.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <windows.h>

namespace uo::render {

namespace {
constexpr int kScratchW = 512;

u32 MakeBGRA(u8 r, u8 g, u8 b, u8 a) {
    return (static_cast<u32>(a) << 24) |
           (static_cast<u32>(r) << 16) |
           (static_cast<u32>(g) << 8) |
            static_cast<u32>(b);
}

void Unpack555(u16 c, u8* r, u8* g, u8* b) {
    *r = static_cast<u8>(((c >> 10) & 31) * 255 / 31);
    *g = static_cast<u8>(((c >> 5) & 31) * 255 / 31);
    *b = static_cast<u8>((c & 31) * 255 / 31);
}
}  // namespace

struct TextRenderer::Impl {
    HDC dc = nullptr;
    HFONT font = nullptr;
    HGDIOBJ oldFont = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* bits = nullptr;
    int pixelHeight = 0;
    int lineHeight = 0;
    int scratchH = 0;
};

TextRenderer::TextRenderer() = default;

TextRenderer::~TextRenderer() {
    if (!impl_) return;
    if (impl_->dc) {
        if (impl_->oldFont) SelectObject(impl_->dc, impl_->oldFont);
        if (impl_->oldBitmap) SelectObject(impl_->dc, impl_->oldBitmap);
    }
    if (impl_->font) DeleteObject(impl_->font);
    if (impl_->bitmap) DeleteObject(impl_->bitmap);
    if (impl_->dc) DeleteDC(impl_->dc);
}

bool TextRenderer::Init(int pixelHeight) {
    impl_ = std::make_unique<Impl>();
    impl_->pixelHeight = pixelHeight;
    impl_->lineHeight = pixelHeight + 3;
    impl_->scratchH = std::max(16, impl_->lineHeight + 2);

    impl_->dc = CreateCompatibleDC(nullptr);
    if (!impl_->dc) {
        impl_.reset();
        return false;
    }

    impl_->font = CreateFontA(pixelHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              "Arial");
    if (!impl_->font) {
        impl_.reset();
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kScratchW;
    bmi.bmiHeader.biHeight = -impl_->scratchH;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    impl_->bitmap = CreateDIBSection(impl_->dc, &bmi, DIB_RGB_COLORS,
                                     &impl_->bits, nullptr, 0);
    if (!impl_->bitmap || !impl_->bits) {
        impl_.reset();
        return false;
    }

    impl_->oldFont = SelectObject(impl_->dc, impl_->font);
    impl_->oldBitmap = SelectObject(impl_->dc, impl_->bitmap);
    SetBkColor(impl_->dc, RGB(0, 0, 0));
    SetTextColor(impl_->dc, RGB(255, 255, 255));
    SetBkMode(impl_->dc, OPAQUE);
    return true;
}

int TextRenderer::Measure(const std::string& s) const {
    if (!impl_ || !impl_->dc || s.empty()) return 0;
    SIZE sz{};
    if (!GetTextExtentPoint32A(impl_->dc, s.c_str(), static_cast<int>(s.size()), &sz))
        return 0;
    return sz.cx;
}

int TextRenderer::LineHeight() const {
    return impl_ ? impl_->lineHeight : 0;
}

void TextRenderer::Draw(Renderer& r, const std::string& s, int x, int y,
                        u16 rgb555, Align align, bool shadow) {
    if (!impl_ || !impl_->dc || !impl_->bits || s.empty()) return;

    const int measuredW = Measure(s);
    if (measuredW <= 0) return;
    if (align == Align::Center) x -= measuredW / 2;

    PatBlt(impl_->dc, 0, 0, kScratchW, impl_->scratchH, BLACKNESS);
    TextOutA(impl_->dc, 0, 0, s.c_str(), static_cast<int>(s.size()));

    const int drawW = std::min(measuredW + 2, kScratchW);
    const int drawH = impl_->scratchH;
    std::vector<u32> rgba(static_cast<usize>(drawW) * drawH);
    const auto* src = static_cast<const u8*>(impl_->bits);

    auto buildLayer = [&](u8 r8, u8 g8, u8 b8) {
        for (int row = 0; row < drawH; ++row) {
            for (int col = 0; col < drawW; ++col) {
                const u8* px = src + (static_cast<usize>(row) * kScratchW + col) * 4;
                const u8 a = px[0];  // white-on-black AA coverage; B/G/R are equal.
                rgba[static_cast<usize>(row) * drawW + col] = MakeBGRA(r8, g8, b8, a);
            }
        }
    };

    if (shadow) {
        buildLayer(0, 0, 0);
        r.BlendRGBA(rgba.data(), drawW, drawH, x + 1, y + 1);
    }

    u8 rr = 255, gg = 255, bb = 255;
    Unpack555(rgb555, &rr, &gg, &bb);
    buildLayer(rr, gg, bb);
    r.BlendRGBA(rgba.data(), drawW, drawH, x, y);
}

}  // namespace uo::render
