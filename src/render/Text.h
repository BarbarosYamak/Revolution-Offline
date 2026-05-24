#pragma once

#include "uo/types.h"

#include <memory>
#include <string>

namespace uo::render {

class Renderer;

class TextRenderer {
public:
    enum class Align { Left, Center };

    TextRenderer();
    ~TextRenderer();

    bool Init(int pixelHeight);
    int Measure(const std::string& s) const;
    int LineHeight() const;

    void Draw(Renderer& r, const std::string& s, int x, int y, u16 rgb555,
              Align align = Align::Left, bool shadow = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace uo::render
