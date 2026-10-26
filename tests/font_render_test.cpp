// 文件：tests/font_render_test.cpp
// 作用：在真实 OpenGL 后端验证字体纹理上传、DPI、图集容量和绘制顺序。
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include "tinyengine/text/unicode.h"
#include "tinyengine/ui/ui_system.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

using namespace tinyengine;

namespace
{
int failures = 0;
void check(const bool value, const char* message)
{
    if (!value)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
struct Pixels
{
    int width = 0, height = 0;
    std::vector<unsigned char> bytes;
    std::size_t bright(int left, int top, int right, int bottom) const
    {
        std::size_t count = 0;
        for (int y = std::max(0, top); y < std::min(height, bottom); ++y)
            for (int x = std::max(0, left); x < std::min(width, right); ++x)
            {
                const auto index = (static_cast<std::size_t>(height - 1 - y) * width + x) * 4;
                count += bytes[index] > 110 || bytes[index + 1] > 110 || bytes[index + 2] > 110;
            }
        return count;
    }
    void save(const std::filesystem::path& path) const
    {
        std::vector<unsigned char> topDown(bytes.size());
        for (int row = 0; row < height; ++row)
            std::copy_n(bytes.data() + static_cast<std::size_t>(height - 1 - row) * width * 4, width * 4,
                        topDown.data() + static_cast<std::size_t>(row) * width * 4);
        // 窗口帧缓冲的 alpha 不参与桌面显示，导出时固定为不透明。
        for (std::size_t index = 3; index < topDown.size(); index += 4)
            topDown[index] = 255;
        auto* surface =
            SDL_CreateRGBSurfaceWithFormatFrom(topDown.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
        if (surface)
        {
            SDL_SaveBMP(surface, toUtf8Path(path).c_str());
            SDL_FreeSurface(surface);
        }
    }
};
Pixels capture(SdlGlRenderDevice& device)
{
    const auto viewport = device.getViewportSize();
    Pixels pixels{static_cast<int>(viewport.x), static_cast<int>(viewport.y)};
    pixels.bytes.resize(static_cast<std::size_t>(pixels.width) * pixels.height * 4);
    glReadPixels(0, 0, pixels.width, pixels.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.bytes.data());
    return pixels;
}
void clear()
{
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.03f, 0.045f, 0.065f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
} // namespace

int main()
{
    SdlGlRenderDevice device(800, 500, "Font render regression");
    if (!device.initialize())
    {
        std::cerr << "FAIL: real OpenGL context unavailable\n";
        return 1;
    }
    SDL_HideWindow(static_cast<SDL_Window*>(device.getNativeWindow()));
    FontManager fonts;
    const auto latin = fonts.loadFace("tests/fixtures/fonts/noto_sans.ttf");
    const auto arabic = fonts.loadFace("tests/fixtures/fonts/noto_sans_arabic.ttf");
    const auto chinese = fonts.loadFace("tests/fixtures/fonts/noto_sans_sc_subset.otf");
    check(latin && arabic && chinese, "font fixtures load");
    if (!latin || !arabic || !chinese)
        return 1;
    fonts.registerFamily("latin", {latin});
    fonts.registerFamily("arabic", {arabic});
    fonts.registerFamily("chinese", {chinese});
    fonts.setDefaultFamily("latin");
    fonts.setFallbackFamilies({"arabic", "chinese"});
    UISystem ui;
    ui.setFontManager(&fonts);
    const auto viewport = device.getViewportSize();
    ui.setViewportSize(viewport.x, viewport.y);
    ui.AddLabel("title", "Font system: AV office fi / iii WWW", {20, 12, 760, 48}).fontSize = 26;
    ui.AddLabel("cjk", Unicode::encode({0x4e2d, 0x6587, 0x5f15, 0x64ce, 0xff0c, 0x5b57, 0x4f53, 0x6d4b, 0x8bd5}),
                {20, 70, 760, 48})
        .fontSize = 28;
    ui.AddLabel("arabic", Unicode::encode({0x0633, 0x0644, 0x0627, 0x0645}) + " 123 ABC", {20, 128, 760, 48}).fontSize =
        28;
    ui.AddLabel("rich", "[color=#ee7755]AV fi[/color] [size=36]Mixed[/size] [size=18]sizes e\xcc\x81[/size]",
                {20, 186, 760, 60})
        .fontSize = 24;
    ui.AddInputField("input", "Selected text and IME", {20, 270, 500, 50});
    ui.focusNext();
    ui.setSelection("input", 0, 8);
    clear();
    device.drawUi(ui.BuildDrawList());
    const auto pixels = capture(device);
    check(pixels.bright(20, 12, 650, 60) > 200, "Latin glyph atlas produces visible ink");
    check(pixels.bright(20, 70, 400, 118) > 250, "CJK fallback produces visible ink");
    check(pixels.bright(20, 128, 790, 176) > 200, "shaped Arabic and digits produce visible ink");
    check(pixels.bright(180, 282, 350, 314) > 20, "input text must contrast with its independent background color");
    const auto output = std::filesystem::path("saved/FontTests");
    std::filesystem::create_directories(output);
    pixels.save(output / "font_multilingual.bmp");
    const auto initial = device.getGlyphAtlasStats();
    device.drawUi(ui.BuildDrawList());
    check(device.getGlyphAtlasStats().glyphCount == initial.glyphCount, "same frame content reuses glyph entries");

    UISystem dpi;
    dpi.setFontManager(&fonts);
    dpi.setViewportSize(viewport.x, viewport.y);
    dpi.AddLabel("dpi", "WWW iii AV", {10, 10, 300, 50});
    clear();
    device.drawUi(dpi.BuildDrawList());
    const auto count1 = capture(device).bright(0, 0, 620, 150);
    dpi.setDpiScale(2);
    clear();
    device.drawUi(dpi.BuildDrawList());
    const auto dpi2 = capture(device);
    const auto count2 = dpi2.bright(0, 0, 620, 150);
    check(count2 > count1 * 2 && count2 < count1 * 6, "DPI rerasterization increases true ink area");
    dpi2.save(output / "font_dpi_2.bmp");

    auto overlap =
        std::make_shared<TextLayoutResult>(TextLayout::shape("AAA", 0, {}, TextDirection::Auto, false, &fonts));
    for (std::size_t index = 0; index < overlap->glyphs.size(); ++index)
    {
        overlap->glyphs[index].x = 5;
        overlap->glyphs[index].style.color = index == 1 ? Vec4{0, 1, 0, 1} : Vec4{1, 0, 0, 1};
    }
    UIDrawCommand over{UIWidgetType::Label, {20, 10, 150, 60}, {1, 1, 1, 1}, "AAA", {}, 18, {0, 0, 800, 500}};
    over.textLayout = overlap;
    clear();
    device.drawUi({over});
    const auto order = capture(device);
    std::size_t red = 0, green = 0;
    for (std::size_t index = 0; index < order.bytes.size(); index += 4)
    {
        red += order.bytes[index] > 180 && order.bytes[index + 1] < 40;
        green += order.bytes[index + 1] > 180 && order.bytes[index] < 40;
    }
    check(red > 10 && green == 0, "nonadjacent equal colors must retain painter order");

    std::vector<std::uint32_t> covered;
    for (std::uint32_t codepoint = 33; codepoint < 0x500; ++codepoint)
        if (latin->getGlyphIndex(codepoint) != 0)
            covered.push_back(codepoint);
    std::vector<UIDrawCommand> many;
    for (const float size : {18.0f, 60.0f, 84.0f, 108.0f})
    {
        TextStyle style;
        style.size = size;
        auto layout = std::make_shared<TextLayoutResult>(
            TextLayout::shape(Unicode::encode(covered), 760, style, TextDirection::LeftToRight, false, &fonts));
        UIDrawCommand command{UIWidgetType::Label, {0, 0, 760, 500}, {1, 1, 1, 1}, "many", {}, size, {0, 0, 800, 500}};
        command.textLayout = std::move(layout);
        many.push_back(std::move(command));
    }
    clear();
    device.drawUi(many);
    const auto expanded = device.getGlyphAtlasStats();
    check(expanded.glyphCount > 441 && expanded.pageCount > 1, "renderer supports many glyphs and atlas pages");
    check(expanded.byteSize <= 32 * 1024 * 1024, "font GPU atlas stays inside its budget");
    check(glGetError() == GL_NO_ERROR, "font uploads and draws must not generate OpenGL errors");
    SDL_Event commit{};
    commit.type = SDL_TEXTINPUT;
    SDL_strlcpy(commit.text.text, "a", sizeof(commit.text.text));
    SDL_Event composition{};
    composition.type = SDL_TEXTEDITING;
    SDL_strlcpy(composition.edit.text, "ni", sizeof(composition.edit.text));
    composition.edit.start = 1;
    SDL_PushEvent(&commit);
    SDL_PushEvent(&composition);
    check(device.pumpEvents(), "text event pump remains active");
    const auto& events = device.GetInputState().textEvents;
    check(events.size() >= 2 && events[events.size() - 2].type == TextInputEvent::Type::Commit &&
              events.back().type == TextInputEvent::Type::Composition && events.back().text == "ni",
          "SDL commit and composition preserve original event order");
    device.shutdown();
    std::cout << "Font OpenGL regressions: " << (failures ? "failed" : "passed") << '\n';
    return failures ? 1 : 0;
}
