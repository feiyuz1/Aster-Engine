# 字体与文字系统

运行时通过 FreeType 读取字体并生成灰度字形，通过 HarfBuzz 塑形，通过 ICU 分析双向文本、字素和换行边界。
CPU 排版与渲染使用相同字体实例和像素尺寸。编辑器的 ImGui 字体图集独立维护。

## 项目字体

在 `.tinyproject` 中配置相对项目根目录的字体路径：

```ini
text.font=assets/fonts/interface.ttf
text.fontBold=assets/fonts/interface_bold.ttf
text.fontItalic=assets/fonts/interface_italic.ttf
text.fontBoldItalic=assets/fonts/interface_bold_italic.ttf
text.fallbackFontCount=2
text.fallbackFont.0=assets/fonts/cjk.otf
text.fallbackFont.1=assets/fonts/arabic.ttf
```

粗体/斜体路径可省略；缺少对应字体面时使用该家族的现有字体面，不合成字重或倾斜。
默认字体留空的旧项目继续发现系统字体。指定默认字体后，项目应显式提供所需的回退字体，
以便在没有系统字体的发布环境中得到一致结果。

发布器检查所有配置字体的有效性，并要求其位于项目资产目录内。
字体原始字节随资产 Cook/TPAK 保存，通过 `PlatformFile` 挂载读取。
发布时同时携带文字库的运行时依赖与许可文件；字体本身的再分发许可由项目随字体保留。

## C++ 使用

```cpp
const auto& context = engine.GetContext();
auto& ui = *context.uiSystem;
auto& label = ui.AddLabel("greeting", "Hello 中文", {20, 20, 400, 50});
label.fontSize = 24.0f;          // UI 逻辑像素；DPI 由 UISystem 应用
label.font.weight = 700;
label.font.language = "zh-Hans";
label.textAlignment = TextAlignment::Center;

auto* fonts = ui.getFontManager();
const auto fontPath = context.resourceManager->resolveAssetPath("assets/fonts/title.ttc");
auto face = fonts->loadFace(tinyengine::fromUtf8Path(fontPath), 1);
fonts->registerFamily("title", {face});
label.font.family = "title";
```

独立工具可创建 `FontManager`，注入 `UISystem::setFontManager()` 或作为 `TextLayout::shape()` 最后一个参数。
`FontAsset` 支持 `ResourceManager::load<FontAsset>()`；其 `getFace()` 返回共享字体实例。
字体实例拥有字节和原生资源，旧排版结果持有旧实例，所以重新加载不会悬空。
`FontManager::reloadChanged()` 在帧边界轮询文件/挂载指纹，修订号变化使 UI 排版缓存失效。
Engine 每 60 帧轮询一次；外部工具按自身刷新周期调用。

## 排版和绘制

`TextLayoutResult` 包含字形、行、墨迹边界、光标与选区几何。
`ShapedGlyph::glyphId` 是字体内部字形标识，必须与 `face` 共同使用；`codepoint` 仅保留源字符信息。
`cluster`/`clusterEnd` 使用原始 UTF-8 字节位置，`x`/`y` 是已含塑形偏移的字形基线位置。
渲染矩形左上角为 `x + bearingX, y - bearingY`。

富文本支持 `[b]`、`[i]`、`[size=24]`、`[color=#ff8800]`、`[font=title]` 及对应闭合标签。
非法或不匹配标签作为普通文字显示。输入框始终按普通文本处理，避免用户输入被解释为样式。

正常换行使用 ICU 给出的合法边界；单个不可断片段宽于容器时，允许按字素紧急换行，
单个字素本身超过容器时保留完整字素并由裁剪处理。
LF、CRLF、CR、NEL、Unicode 行/段分隔符都支持硬换行。
连字中的光标位置按组成字素等分，尚未读取字体 GDEF 中的精细连字光标位置。

`GlyphAtlas` 默认每页 1024×1024 RGBA、最多 8 页/32 MiB，按真实字形尺寸加边距打包。
缓存区分字体实例、glyphId 和 1/64 像素精度的字号；颜色不进入字形缓存。
当预算耗尽时当前帧显示明确占位，下一帧开始重置代际；不会在一帧中改写已分配 UV。
持续超预算的页面应减少同时显示的字号/字形，或配置更合适的图集预算。

## 输入与诊断

输入框支持字素删除、可见顺序的左右光标、Home/End、Shift 选区、Ctrl+A、鼠标定位、水平滚动和 IME 预编辑。
SDL 提交和预编辑事件保留原始顺序。文字选择、输入法下划线与候选框位置使用排版几何。

Engine 诊断指标包含 `text.layout_builds`、`text.atlas_pages`、`text.atlas_bytes`、
`text.atlas_glyphs` 和 `text.atlas_overflows`。
测试字体位于 `tests/fixtures/fonts`；`tinyengine_font_render` 使用真实 OpenGL 读取像素，
并在 `saved/FontTests` 输出多语言和 DPI 截图。

当前范围为横排灰度位图文字。彩色 emoji、竖排、可变字体轴控制、合成字重/倾斜和 SDF/MSDF 留作扩展。
Windows x64 Debug/Release 是本次验证平台，其他平台需按构建指南提供 ICU 包并进行对应验收。
