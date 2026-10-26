#pragma once

class EngineRuntime;
class World;

// 编辑器视口相机：右键拖拽旋转、中键拖拽平移。只做相机数学，不持有场景，
// 也不管窗口/target 尺寸（平移需要的视口高度由调用方按当前渲染 target 传入）。
class ViewportCamera
{
public:
    void RotateCamera(EngineRuntime& runtime, World& world, float deltaX, float deltaY);
    void PanCamera(EngineRuntime& runtime, World& world, float deltaX, float deltaY, float viewportHeight);

private:
    // 平移的参考距离（米）：中键拖拽按它把像素换算成世界单位。
    float m_focusDistance = 10.0f;
};
