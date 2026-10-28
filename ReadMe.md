# 小型游戏引擎

一个使用C++编写的模块化3D游戏引擎，包含完整的编辑器，项目资产管理体系，可执行程序与运行时环境。

## 截图

![](https://github.com/HYIND/HYEngine/blob/master/Screenshots/Screenshot1.png?raw=true)
![](https://github.com/HYIND/HYEngine/blob/master/Screenshots/Screenshot2.png?raw=true)
![](https://github.com/HYIND/HYEngine/blob/master/Screenshots/Screenshot3.png?raw=true)
![](https://github.com/HYIND/HYEngine/blob/master/Screenshots/Screenshot4.png?raw=true)

## ✨ 核心功能

### 架构设计
- ECS 架构
- 模块化设计，引擎各模块可独立编译，Gameplay和多媒体运行时解耦，低耦合
- 运行时动态编译Render Graph，自动处理渲染Pass的时序和资源依赖，分析并进行基于时序的自动资源复用

### Vulkan渲染
- 延迟渲染管线，支持多光源渲染、基于间接调用的批量渲染
- 运行时编译RenderGraph，自动推导 Pass 的执行时序与资源依赖，实现并行录制、基于生命周期复用资源
- 后处理效果，SSR、SSGI、RayTrace等
- 基于预计算的大气散射渲染
- 基于步进的体积光实现
- 基于距离排序的半透明渲染

### 引擎框架和资产集成
- 提供可连接多种宿主窗口的引擎框架，可选的渲染、引擎、输入服务
- 通过动态加载ModuleDLL实现用户侧逻辑的动态注册
- 集成支持多种资产类型（纹理, 引擎, 材质, 网格体, 模型, 场景, 脚本, UI, UI样式）的项目管理运行时，可通过编辑器进行项目和资产操作
- 项目资产系统和引擎集成，实现打开资产场景、切换场景等功能
- 通过引擎事件总线实现UI、Module和宿主窗口的多端灵活交互
- lua脚本支持

### 物理引擎
- 集成 **Bullet Physics** 3D 物理引擎
- 支持刚体、角色
- 碰撞检测与响应，通过事件系统实现响应解耦，与Transform集成

### 编辑器
- 基于 **Dear ImGui** 的场景编辑器
- 基于项目管理资产，支持保存与加载项目，可导入资产，结合资产浏览器配置资产
- 基于 **reflect-cpp** 的基本编译期反射，实现组件属性实时编辑与查看
- 动态加载Module实现注册外部模块、系统和组件

### 音频系统
- 3D 空间音频定位
- 多声道支持
- 音频资源管理

## 目录结构

- `bin/` – 可执行文件及运行时资源（着色器、测试场景等）
- `Common/` – 公共基础库（ECS核心、基础物理系统、工具类、辅助函数）
- `AssetRuntime/` – 项目资产管理运行时（动态资产注册、解析、持久化）
- `PlatformRuntime/` – 引擎运行时系统（音频、输入、渲染、动态Module注册、事件总线）
- `VulkanRenderEngine/` – 渲染引擎（渲染图、渲染管线）
- `GamePlayModule/` – 用户自定义Module，实现用户侧逻辑，可定义用户的外部组件、系统等玩法逻辑，或实现业务逻辑，导出dll供引擎运行时动态注册
- `Editor/` – 基于glfw、ImGui的编辑器（基于Module的模块加载实现动态注册组件、系统，动态反射、序列化组件，编辑场景）
- `Game/` – 基于Win32窗口的exe，提供基本的引擎启动逻辑，并和引擎对接窗口事件，通过Module机制注册dll，配合Project机制，实现可变的灵活的玩法修改、场景配置
- `ThirdParty/` – 第三方依赖头文件（[Libs](https://github.com/HYIND/Libs)、Assimp、Bullet、GLFW、GLM等）
- `libs/` – 依赖lib
