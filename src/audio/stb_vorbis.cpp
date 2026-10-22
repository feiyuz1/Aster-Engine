// 文件：src/audio/stb_vorbis.cpp
// 作用：隔离 stb_vorbis 第三方实现，避免其宏和告警污染其他编译单元。
// 约定：Voice 生命周期由音频系统拥有，流式缓冲不得在回调中阻塞。
// 类型：实现文件。

#ifdef TINYENGINE_HAS_STB_VORBIS
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>
#endif
