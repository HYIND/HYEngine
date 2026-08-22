#pragma once

#include "OpenGLRenderEngine/General/IndirectDrawManager.h"

std::shared_ptr<IndirectDrawManager> IndirectDrawManager::Instance()
{
	static std::shared_ptr<IndirectDrawManager> instance = std::shared_ptr<IndirectDrawManager>(new IndirectDrawManager());
	return instance;
}

void IndirectDrawManager::setupMesh(Mesh& mesh)
{
	auto guard = LockGuard(_meshMutex);
	auto uuid = mesh.GetUUID();
	auto version = mesh.GetVerticesIndicesVsrsion();

	{
		SegmentData segmentData;
		if (!_VBOManager.FindSegment(uuid, segmentData) || (uint32_t)segmentData.userData != version)
		{
			auto& vertices = mesh.GetVertices();
			_VBOManager.SetSegment(uuid, (void*)version, vertices.data(), vertices.size() * sizeof(Vertex));
		}
	}

	{
		SegmentData segmentData;
		if (!_EBOManager.FindSegment(uuid, segmentData) || (uint32_t)segmentData.userData != version)
		{
			auto& indices = mesh.GetIndices();
			_EBOManager.SetSegment(uuid, (void*)version, indices.data(), indices.size() * sizeof(unsigned int));
		}
	}
}

void IndirectDrawManager::deleteMesh(Mesh& mesh)
{
	auto guard = LockGuard(_meshMutex);
	_VBOManager.RemoveSegment(mesh.GetUUID());
	_EBOManager.RemoveSegment(mesh.GetUUID());
}

bool IndirectDrawManager::GetIndirectDrawMeta(Mesh& mesh, IndirectDrawMeta& meta)
{
	auto uuid = mesh.GetUUID();
	SegmentData vbodata, ebodata;
	if (!_VBOManager.FindSegment(uuid, vbodata) || !_EBOManager.FindSegment(uuid, ebodata))
		return false;

	meta.indexCount = ebodata.count / sizeof(unsigned int);
	meta.indexfirst = ebodata.first / sizeof(unsigned int);
	meta.vertexFirst = vbodata.first / sizeof(Vertex);

	return true;
}

GLuint IndirectDrawManager::GetVBO() {
	return _VBOManager.GetBuffer()->GetID();
}

GLuint IndirectDrawManager::GetEBO() {
	return _EBOManager.GetBuffer()->GetID();
}

void IndirectDrawManager::setupMaterial(Material& material)
{
	auto guard = LockGuard(_materialMutex);

	auto uuid = material.GetUUID();
	auto version = material.GetVersion();
	SegmentData segmentData;
	if (!_MaterialManager.FindSegment(uuid, segmentData) || (uint32_t)segmentData.userData != version)
	{
		auto data = material.GetMaterialCompData();
		_MaterialManager.SetSegment(uuid, (void*)version, &data, sizeof(data));
	}
}

void IndirectDrawManager::deleteMaterial(Material& material)
{
	auto guard = LockGuard(_materialMutex);
	_MaterialManager.RemoveSegment(material.GetUUID());
}

bool IndirectDrawManager::GetMaterialIndex(Material& material, uint64_t& index)
{
	auto guard = LockGuard(_materialMutex);

	SegmentData materialdata;
	if (!_MaterialManager.FindSegment(material.GetUUID(), materialdata))
		return false;
	index = materialdata.first / sizeof(MaterialData);
	return true;
}

std::shared_ptr<SSBO> IndirectDrawManager::GetSSBO() {
	return _MaterialManager.GetBuffer()->GetSSBO();
}

IndirectDrawManager::IndirectDrawManager() {}

