#pragma once

void RenderLoop(bool* shouldStopRenderThread);

bool IsRenderThread();

void WaitForInitScene3D();
bool IsInitScene3D();