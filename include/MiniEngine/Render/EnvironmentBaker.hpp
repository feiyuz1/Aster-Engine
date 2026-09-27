// 文件：include/MiniEngine/Render/EnvironmentBaker.hpp
// 作用：离线生成环境光照所需的辐照度、预过滤立方体贴图和 BRDF 查找表。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
struct EnvironmentBakeResult
{
    bool success = false;
    std::filesystem::path environmentPath;
    std::filesystem::path irradiancePath;
    std::filesystem::path prefilterPath;
    std::filesystem::path brdfLutPath;
    std::string error;
};

class EnvironmentBaker
{
  public:
    /// @brief 烘焙环境贴图、辐照度、预过滤贴图和 BRDF LUT。
    /// @param source 源数据、源资源或源位置。
    /// @param outputDirectory 生成文件的输出目录。
    /// @param sampleCount 目标尺寸、数量、索引或限制值。
    /// @return 返回接口计算或查询得到的结果。
    static EnvironmentBakeResult Bake(const std::filesystem::path &source, const std::filesystem::path &outputDirectory,
                                      int sampleCount = 64);
};
} // 结束 MiniEngine 命名空间
