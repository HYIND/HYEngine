# 小型游戏引擎

一个使用C++编写的模块化3D游戏引擎，包含完整的编辑器，简单游戏本体与运行时环境。

## 截图

![](https://github.com/HYIND/HYEngine/blob/master/Screenshots/Screenshot1.png?raw=true)
![](https://github.com/HYIND/HYEngine/blob/master/Screenshots/Screenshot2.png?raw=true)

## ✨ 核心功能

### 架构设计
- ECS 架构
- 模块化设计，引擎各模块可独立编译，Gameplay和多媒体运行时解耦，低耦合
- 运行时动态编译Render Graph，自动处理渲染Pass的时序和资源依赖，分析并进行基于时序的自动资源复用

### Vulkan渲染
- 混合渲染，结合延迟渲染与向前渲染，支持多光源渲染，支持间接调用的批量渲染
- 后处理效果，SSR、SSGI、RayTrace等
- 基于预计算的大气散射渲染
- 基于步进的体积光实现
- 基于距离排序的半透明渲染

### 物理引擎
- 集成 **Bullet Physics** 3D 物理引擎
- 支持刚体、角色
- 碰撞检测与响应、与Transform集成

### 编辑器
- 基于 **Dear ImGui** 的场景编辑器
- 基于项目管理资产，支持保存与加载项目，可导入资产，结合资产浏览器配置资产
- 基于 **reflect-cpp** 的基本编译期反射，实现组件属性实时编辑与查看
- 动态加载Module实现注册外部模块

### 音频系统
- 3D 空间音频定位
- 多声道支持
- 音频资源管理

### 输入系统
- 键盘、鼠标、键位映射管理、焦点管理


## 目录结构

- `bin/` – 可执行文件及运行时资源（着色器、测试场景等）
- `Common/` – 公共基础库（ECS核心、基础物理系统、工具类、辅助函数）
- `AssetRuntime/` – 资产管理运行时（动态资产注册、解析、持久化）
- `PlatformRuntime/` – 引擎运行时系统（音频、输入、渲染、动态Module加载）
- `VulkanRenderEngine/` – 渲染引擎（渲染图、渲染管线）
- `GamePlayModule/` – 游戏玩法系统（运动、交互等玩法逻辑、导出dll实现供运行时动态加载）
- `Editor/` – 基于ImGui的编辑器（基于Module的模块加载实现动态注册组件、系统，动态反射、序列化组件，编辑场景）
- `Game/` – 游戏本体exe（win32窗口事件等）
- `ThirdParty/` – 第三方依赖头文件（[Libs](https://github.com/HYIND/Libs)、Assimp、Bullet、GLFW、GLM等）
- `libs/` – 依赖lib
