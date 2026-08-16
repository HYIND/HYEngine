#pragma once

#include "OpenGLRenderEngine/General/IndirectDrawManager.h"

VBOSegmentBuffer::VBOSegmentBuffer()
	:_VBO(0), _size(0)
{
}

VBOSegmentBuffer::~VBOSegmentBuffer()
{
	if (_VBO > 0)
		glDeleteBuffers(1, &_VBO);
}

void VBOSegmentBuffer::ReSize(uint64_t length)
{
	Need();

	auto& newsize = length;
	if (newsize > _size)
	{
		if (_size > 0)
		{
			GLuint tempBuffer;
			glGenBuffers(1, &tempBuffer);

			glBindBuffer(GL_COPY_READ_BUFFER, _VBO);
			glBindBuffer(GL_COPY_WRITE_BUFFER, tempBuffer);

			glBufferData(GL_COPY_WRITE_BUFFER, _size, nullptr, GL_STATIC_DRAW);

			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, _size);

			glBufferData(GL_ARRAY_BUFFER, newsize, nullptr, GL_DYNAMIC_DRAW);

			glBindBuffer(GL_COPY_READ_BUFFER, tempBuffer);
			glBindBuffer(GL_COPY_WRITE_BUFFER, _VBO);
			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, _size);

			glDeleteBuffers(1, &tempBuffer);
		}
		else
			glBufferData(GL_ARRAY_BUFFER, newsize, nullptr, GL_STATIC_DRAW);

		_size = newsize;
		//std::cout << std::format("VBOSegmentBuffer Resize {}MB\n", newsize / (1024 * 1024));
	}
}

void VBOSegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length)
{
	Need();

	uint64_t needSize = length + first;
	if (needSize > _size)
		ReSize(needSize * 1.5);

	glBufferSubData(GL_ARRAY_BUFFER, first, length, mem);
}

void VBOSegmentBuffer::Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length)
{
	if (length == 0 || destFirst == srcFirst)
		return;

	if (destFirst + length > _size || srcFirst + length > _size)
		return;

	Need();

	// 创建临时缓冲
	GLuint tempBuffer;
	glGenBuffers(1, &tempBuffer);

	// 从原VBO拷贝到临时缓冲
	glBindBuffer(GL_COPY_READ_BUFFER, _VBO);
	glBindBuffer(GL_COPY_WRITE_BUFFER, tempBuffer);
	glBufferData(GL_COPY_WRITE_BUFFER, length, nullptr, GL_STATIC_DRAW);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcFirst, 0, length);

	// 从临时缓冲拷回原VBO的目标位置
	glBindBuffer(GL_COPY_READ_BUFFER, tempBuffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, _VBO);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, destFirst, length);

	// 清理
	glDeleteBuffers(1, &tempBuffer);
}

GLuint VBOSegmentBuffer::GetVBO() const
{
	return _VBO;
}

void VBOSegmentBuffer::Need()
{
	if (_VBO == 0)
		glGenBuffers(1, &_VBO);
	glBindBuffer(GL_ARRAY_BUFFER, _VBO);
}

EBOSegmentBuffer::EBOSegmentBuffer()
	:_EBO(0), _size(0)
{
}

EBOSegmentBuffer::~EBOSegmentBuffer()
{
	if (_EBO > 0)
		glDeleteBuffers(1, &_EBO);
}

void EBOSegmentBuffer::ReSize(uint64_t length)
{
	Need();

	auto& newsize = length;
	if (newsize > _size)
	{
		if (_size > 0)
		{
			GLuint tempBuffer;
			glGenBuffers(1, &tempBuffer);

			glBindBuffer(GL_COPY_READ_BUFFER, _EBO);
			glBindBuffer(GL_COPY_WRITE_BUFFER, tempBuffer);

			glBufferData(GL_COPY_WRITE_BUFFER, _size, nullptr, GL_STATIC_DRAW);

			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, _size);

			glBufferData(GL_ELEMENT_ARRAY_BUFFER, newsize, nullptr, GL_DYNAMIC_DRAW);

			glBindBuffer(GL_COPY_READ_BUFFER, tempBuffer);
			glBindBuffer(GL_COPY_WRITE_BUFFER, _EBO);
			glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, _size);

			glDeleteBuffers(1, &tempBuffer);
		}
		else
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, newsize, nullptr, GL_STATIC_DRAW);

		_size = newsize;
		//std::cout << std::format("EBOSegmentBuffer Resize {}MB\n", newsize / (1024 * 1024));
	}
}

void EBOSegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length)
{
	Need();

	uint64_t needSize = length + first;
	if (needSize > _size)
		ReSize(needSize * 1.5);

	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, first, length, mem);
}

void EBOSegmentBuffer::Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length)
{
	if (length == 0 || destFirst == srcFirst)
		return;

	if (destFirst + length > _size || srcFirst + length > _size)
		return;

	Need();

	// 创建临时缓冲
	GLuint tempBuffer;
	glGenBuffers(1, &tempBuffer);

	// 从原VBO拷贝到临时缓冲
	glBindBuffer(GL_COPY_READ_BUFFER, _EBO);
	glBindBuffer(GL_COPY_WRITE_BUFFER, tempBuffer);
	glBufferData(GL_COPY_WRITE_BUFFER, length, nullptr, GL_STATIC_DRAW);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcFirst, 0, length);

	// 从临时缓冲拷回原VBO的目标位置
	glBindBuffer(GL_COPY_READ_BUFFER, tempBuffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, _EBO);
	glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, destFirst, length);

	// 清理
	glDeleteBuffers(1, &tempBuffer);
}

GLuint EBOSegmentBuffer::GetEBO() const
{
	return _EBO;
}

void EBOSegmentBuffer::Need()
{
	if (_EBO == 0)
		glGenBuffers(1, &_EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _EBO);
}

SSBOSegmentBuffer::SSBOSegmentBuffer() {
	_ssbo = std::make_shared<SSBO>();
}

SSBOSegmentBuffer::~SSBOSegmentBuffer() {}

void SSBOSegmentBuffer::ReSize(uint64_t length) {
	_ssbo->SetSize(length);
}

void SSBOSegmentBuffer::WriteData(const void* mem, uint64_t first, uint64_t length) {
	_ssbo->WriteData(mem, length, first);
}

void SSBOSegmentBuffer::Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length) {
	_ssbo->CopyData(destFirst, srcFirst, length);
}

std::shared_ptr<SSBO> SSBOSegmentBuffer::GetSSBO() const {
	return _ssbo;
}

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
	return _VBOManager.GetBuffer()->GetVBO();
}

GLuint IndirectDrawManager::GetEBO() {
	return _EBOManager.GetBuffer()->GetEBO();
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

