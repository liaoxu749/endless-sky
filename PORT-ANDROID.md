# Endless Sky 安卓移植成本评估

结论先行：**渲染层最大风险已被上游消除，移植是"工程活"而非"重构活"**。
总成本约 **15–25 人日**（业余时间 2–4 周），略高于联机功能本身（M1 约 5–10 人日）。
没有找到现成的安卓移植项目（GitHub 检索 11 个结果均无关），但上游已内置关键支撑。

## 关键发现：上游已支持 OpenGL ES 3

`ES_GLES` 是上游一等公民的 CMake 开关（`CMakeLists.txt:9`），覆盖 15+ 处代码：

- `opengl.h:22`：ES_GLES 下直接用 `<GLES3/gl3.h>`，跳过 GLEW；
- `GameWindow.cpp:227`：GLES 上下文（`SDL_GL_CONTEXT_PROFILE_ES`，GL 3.0）；
- `shader/Shader.cpp:107`：GLSL 版本号按运行时 `GL_SHADING_LANGUAGE_VERSION` 自动生成；
- `Sprite.cpp` / `GameData.cpp`：纹理路径与功能探测均有 GLES 分支；
- 全库 grep 未见 `glPolygonMode` 等桌面专属调用。

也就是说：游戏本来就能跑在 GLES3 设备上（上游用它支持树莓派等 ARM 设备）。
渲染层从"最大风险"降级为"编译选项"。

## 工作项与估算

| # | 工作项 | 估算 | 说明 |
|---|--------|------|------|
| 1 | android-project 胶水（Gradle + NDK + SDL2 Activity + 清单 + 资源打包） | 1–2 天 | SDL 官方有 android-project 模板可抄 |
| 2 | 依赖的 NDK 交叉编译（SDL2_image/libpng/libjpeg/openal-soft/libmad/flac/vorbis/minizip） | 1–3 天 | vcpkg 有 arm64-android 三元组，或手工编 |
| 3 | Files.cpp 安卓存储路径分支 | 半天 | SDL_AndroidGetInternalStoragePath 系列接口 |
| 4 | **触屏操作层**（大头）：SDL 默认把单指触摸合成鼠标事件，飞行/点选天然可用；缺的是键盘快捷键 → 屏幕虚拟按键层（跳跃/降落/开火/菜单等约 20 个键） | 3–7 天 | 用 ES 的 Interface 系统画按键层，或独立覆盖面板 |
| 5 | 生命周期处理（切后台 GL 上下文丢失 → SpriteSet 重载） | 1–2 天 | 调试为主 |
| 6 | 小屏 UI 缩放调优 | 1–2 天 | ES 接口系统自带缩放，主要是验证 |
| 7 | 音频（openal-soft 自带安卓后端） | 半天 | 基本零改动 |
| 8 | ESMP 联机层适配 | ≈0 | POSIX socket / UDP 广播在安卓可用 |
| 9 | 真机性能与兼容验证（不同 GPU/OEM 广播权限差异） | 1–2 天 | 广播发现可能被部分厂商省电策略拦截 |

## 风险

- NDK/vcpkg 工具链 yak-shaving 占比高，且必须真机反复装包调试；
- 触屏手感需要多轮迭代（飞行是鼠标精确瞄准玩法）；
- 广播发现可能被部分手机的网络策略限制（备选：直填 IP 已实现）。

## 在哪构建

- **本手机（proot）**：所有源码修改、构建配置、CI 配置都可以在这里完成；
  手机上直接出 APK 理论可行（NDK ~2GB + openjdk 经 apt 安装），但编译慢、调试体验差；
- **推荐路径**：这里改好代码 → 写 GitHub Actions 工作流（ubuntu runner + NDK 自动出 APK）→
  你只要把 fork 推上 GitHub，APK 从 Actions 产物下载安装，电脑都不用碰。

## 与其他路线对比

| 路线 | 到"能玩"的距离 | 备注 |
|------|----------------|------|
| 电脑实测 ESMP 联机 | 0（补丁已就绪） | 已交付 |
| 移植安卓 | 15–25 人日 | 本文档 |
| 换 Shattered PD（安卓原生） | 联机开发 10–20 人日 | 无移植成本，但要重做联机 |
