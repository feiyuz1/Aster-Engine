# Assimp FBX/glTF 导入兼容报告

同步日期：2026-07-16。测试源来自本机 `D:/OpenSourceProject/assimp-master/test/models`，由 `tinyengine_import_compatibility_tests` 实际导入，不把第三方模型复制进仓库。

| 用例 | 结果 | Mesh | Material | 源动画 | 骨骼 | 导出 Clip | 顶点 | 三角形 | 压缩后曲线键比例 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| FBX 骨骼动画 | PASS | 1 | 1 | 1 | 14 | 1 | 2464 | 2112 | 8.88% |
| FBX 复杂多网格 | PASS | 19 | 4 | 0 | 0 | 0 | 1507 | 1368 | 不适用 |
| glTF 2.0 Skin | PASS | 1 | 1 | 1 | 2 | 1 | 10 | 8 | 1.81% |
| glTF 2.0 复杂 PBR 场景 | PASS | 34 | 35 | 0 | 0 | 0 | 55843 | 75730 | 不适用 |

测试除检查导入返回值外，还会逐一验证 Prefab、Mesh、Skeleton 和 Animation 文件存在，Mesh 为 `TINY_MESH 2` 且不含 NaN，Skeleton 合法且不超过 64 骨骼，Clip 可重新加载并确实使用压缩曲线。

运行方式：

```powershell
cmake --build build --config Release --target tinyengine_import_compatibility_tests
ctest --test-dir build -C Release -R tinyengine_import_compatibility --output-on-failure
```

每次运行还会在 `Saved/ImportCompatibility/REPORT.md` 生成本机实测报告。当前硬边界是 GPU Shader 最多 64 根骨骼、每顶点四权重；超出的骨骼会被诊断并记录在导入警告中。测试矩阵不代表所有 DCC 导出器和所有 FBX/glTF 扩展均已覆盖。
