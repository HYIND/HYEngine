#pragma once

#include "RenderGraphContext.h"
#include "ResourcePool.h"
#include "RenderGraphResourceManager.h"
#include "../RenderPass/RenderPassBase.h"
#include <unordered_set>

namespace OpenGLRenderGraph
{
	template<typename... Resources>
	auto PassNode::Input(Resources&&... resources) ->
		typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type{
		(Input(std::forward<Resources>(resources)), ...);
		return *this;
	};
	template<typename... Resources>
	auto PassNode::InputOption(Resources&&... resources) ->
		typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type{
		(InputOption(std::forward<Resources>(resources)), ...);
		return *this;
	};
	template<typename... Resources>
	auto PassNode::Output(Resources&&... resources) ->
		typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type{
		(Output(std::forward<Resources>(resources)), ...);
		return *this;
	};
	template<typename... Resources>
	auto PassNode::Temp(Resources&&... resources) ->
		typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type{
		(Temp(std::forward<Resources>(resources)), ...);
		return *this;
	};
	template<typename... Resources>
	auto PassNode::Persistent(Resources&&... resources) ->
		typename std::enable_if<(sizeof...(Resources) > 1), PassNode&>::type{
		(Persistent(std::forward<Resources>(resources)), ...);
		return *this;
	};
	template<typename... Passes>
	auto PassNode::After(Passes&&... passes) ->
		typename std::enable_if<(sizeof...(Passes) > 1), PassNode&>::type{
		(After(std::forward<Passes>(passes)), ...);
		return *this;
	};
	template<typename... Passes>
	auto PassNode::Before(Passes&&... passes) ->
		typename std::enable_if<(sizeof...(Passes) > 1), PassNode&>::type{
		(Before(std::forward<Passes>(passes)), ...);
		return *this;
	};
	template<typename... Passes>
	auto PassNode::External(Passes&&... passes) ->
		typename std::enable_if<(sizeof...(Passes) > 1), PassNode&>::type{
		(External(std::forward<Passes>(passes)), ...);
		return *this;
	};
}