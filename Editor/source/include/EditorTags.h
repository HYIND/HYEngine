#pragma once

#include "ECSCore/IComponent.h"

struct TagEditorCamera :public IComponent {};

struct TageEditorPick :public IComponent
{};

struct TagEditorName :public IComponent
{
    std::string name;
};