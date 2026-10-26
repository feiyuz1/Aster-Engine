# 固定字体测试输入

这些字体仅供自动化测试，不作为项目发布时的默认字体。拉丁、阿拉伯字体及测试集合许可见 OFL.txt，
中文子集许可见原始 Noto CJK 项目的 OFL_CJK.txt；版权信息亦保留在字体自身的 name 表中。

- `noto_sans.ttf`：Noto Sans Regular，来源 https://github.com/notofonts/noto-fonts/tree/main/hinted/ttf/NotoSans 。
- `noto_sans_arabic.ttf`：Noto Sans Arabic Regular，同仓库 hinted/ttf/NotoSansArabic。
- `noto_sans_sc_subset.otf`：Noto Sans SC Regular 的小型测试字符子集，来源 https://github.com/notofonts/noto-cjk/tree/main/Sans/SubsetOTF/SC 。
- `noto_test_collection.ttc`：前两个字体组成的测试集合，face 0 为拉丁字体，face 1 为阿拉伯字体。

获取日期：2026-10-26。中文子集由 fontTools 4.60.2 生成，保留字体自身的度量与塑形表。
测试使用固定字节输入，不联网下载，也不要求测试机安装系统字体。

SHA256：

```text
noto_sans.ttf b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5
noto_sans_arabic.ttf ceea25b464a656dc3b26849bab9356740401af62aedf1bfa8b7f0d9b75925b1b
noto_sans_sc_subset.otf cacc41f83c7fce2aa8217c4e1d36b40e73ed56ac3a624839f20d508991ace5de
noto_test_collection.ttc d93e5527510c7a9833a7fdfe78e151dc6e5ef1127f9cad118c69c4c76909f599
```
