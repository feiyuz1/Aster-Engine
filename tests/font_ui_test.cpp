// 文件：tests/font_ui_test.cpp
// 作用：验证输入框按字素编辑及字体样式、DPI 和排版缓存。
#include "tinyengine/text/unicode.h"
#include "tinyengine/ui/ui_system.h"

#include <algorithm>
#include <iostream>

using namespace tinyengine;
int main()
{
    UISystem ui;
    ui.AddInputField("input", "", {0, 0, 200, 50});
    ui.focusNext();
    ui.inputText("e\xcc\x81");
    ui.backspace();
    if (!ui.Find("input")->text.empty())
    {
        std::cerr << "FAIL: backspace must remove the whole combining grapheme\n";
        return 1;
    }
    int failures = 0;
    const auto check = [&](bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };
    FontManager fonts;
    const auto latin = fonts.loadFace("tests/fixtures/fonts/noto_sans.ttf");
    fonts.registerFamily("fixture", {latin});
    fonts.setDefaultFamily("fixture");
    ui.setFontManager(&fonts);
    ui.inputText("abc");
    ui.setSelection("input", 1, 2);
    ui.inputText("X");
    check(ui.Find("input")->text == "aXc", "committed text replaces selected range");
    ui.moveCursor(-1);
    ui.deleteForward();
    check(ui.Find("input")->text == "ac", "forward deletion removes grapheme at caret");
    ui.selectAll();
    ui.inputText("[b]literal[/b]");
    const auto literal = ui.BuildDrawList().front();
    check(literal.textLayout && literal.textLayout->glyphs.size() >= 14,
          "input fields must display rich tags literally");
    const auto before = ui.getTextLayoutBuildCount();
    ui.BuildDrawList();
    check(ui.getTextLayoutBuildCount() == before, "unchanged UI text reuses cached layout");
    ui.setDpiScale(2.0f);
    const auto scaled = ui.BuildDrawList().front();
    check(scaled.fontSize == 36 && scaled.textLayout->height > literal.textLayout->height * 1.5f,
          "DPI changes must rebuild using physical-pixel font size");
    check(ui.getTextLayoutBuildCount() == before + 1, "DPI invalidates exactly affected layout");
    ui.selectAll();
    const auto emoji = Unicode::encode({0x1f469, 0x200d, 0x1f4bb});
    ui.inputText(emoji);
    ui.Find("input")->password = true;
    check(ui.BuildDrawList().front().text == "*", "password masking counts graphemes rather than UTF-8 bytes");
    ui.backspace();
    check(ui.Find("input")->text.empty(), "backspace removes full ZWJ sequence");
    ui.Find("input")->password = false;
    ui.inputText("ab");
    ui.setSelection("input", 1, 1);
    check(ui.setComposition("ni", 1, 1), "input method preedit accepted");
    const auto preedit = ui.BuildDrawList().front();
    check(preedit.text == "anib" && ui.Find("input")->text == "ab" && !preedit.compositionRects.empty(),
          "preedit draws separately without committing source text");
    check(!ui.backspace(), "IME owns editing while preedit active");
    ui.inputText(Unicode::encode({0x4f60}));
    check(ui.Find("input")->text == "a" + Unicode::encode({0x4f60}) + "b" && ui.Find("input")->compositionText.empty(),
          "IME commit inserts once at saved caret");
    ui.Find("input")->enabled = false;
    check(!ui.backspace() && !ui.inputText("x"), "disabled input field must reject edits");
    ui.Find("input")->enabled = true;
    ui.selectAll();
    const auto selected = ui.BuildDrawList().front();
    check(!selected.selectionRects.empty() && selected.caretRect.height > 0,
          "focused selection exposes render geometry");
    check(!ui.inputText(std::string("\xc0\xaf", 2)), "input rejects malformed UTF-8");
    const auto arabic = fonts.loadFace("tests/fixtures/fonts/noto_sans_arabic.ttf");
    fonts.registerFamily("arabic", {arabic});
    fonts.setFallbackFamilies({"arabic"});
    auto* input = ui.Find("input");
    input->text = "abc " + Unicode::encode({0x0633, 0x0644, 0x0627, 0x0645}) + " def";
    ui.setSelection("input", 0, 0);
    for (int index = 0; index < 64; ++index)
        ui.moveCursor(1);
    check(input->caretOffset == input->text.size(), "visual arrows must traverse mixed bidi text without cycling");
    input->text = "abcdef";
    ui.setSelection("input", 1, 4);
    ui.moveCursor(-1);
    check(input->caretOffset == 1 && input->selectionAnchor == 1,
          "left arrow collapses a selection to its visual left edge");
    ui.AddInputField("other", "", {0, 80, 200, 50});
    ui.setComposition("ni", 1, 0);
    ui.focusNext();
    ui.setComposition("");
    ui.focusNext();
    check(ui.Find("input")->compositionText.empty() && ui.backspace(),
          "focus transitions cancel outgoing preedit and preserve editing after return");
    input = ui.Find("input");
    input->text = "this is a long scrolled input field with many words and glyphs";
    ui.setSelection("input", input->text.size(), input->text.size());
    const auto scrolled = ui.BuildDrawList().front();
    ui.focusNext();
    const auto blurred = ui.BuildDrawList().front();
    check(scrolled.textOffset.x < 0 && blurred.textOffset.x == scrolled.textOffset.x,
          "blurred inputs must retain the same displayed and hit-test text origin");
    ui.focusNext();
    ui.selectAll();
    ui.inputText("");
    ui.backspace();
    ui.processTextEvents({{TextInputEvent::Type::Commit, "a"}, {TextInputEvent::Type::Composition, "ni", 1, 0}});
    check(ui.Find("input")->text == "a" && ui.Find("input")->compositionText == "ni",
          "same-frame commit then preedit retains the next syllable");
    ui.processTextEvents({{TextInputEvent::Type::Composition, "hao", 2, 0}, {TextInputEvent::Type::Commit, "b"}});
    check(ui.Find("input")->text == "ab" && ui.Find("input")->compositionText.empty(),
          "same-frame preedit then commit inserts exactly once");
    UISystem clipped;
    clipped.AddPanel("parent", {0, 0, 40, 40});
    clipped.AddLabel("outside", "hidden", {80, 0, 80, 30});
    clipped.setParent("outside", "parent");
    const auto& clippedCommands = clipped.BuildDrawList();
    check(clippedCommands.size() == 1 && clippedCommands.front().type == UIWidgetType::Panel,
          "text outside ancestor clip must not reappear through unspecified-clip fallback");
    std::cout << (failures ? "UI text failures\n" : "UI text regressions passed\n");
    return failures ? 1 : 0;
}
