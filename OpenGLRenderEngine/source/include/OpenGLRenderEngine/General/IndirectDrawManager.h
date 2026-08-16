#pragma once

#include "OpenGLRenderEngine/Base/Mesh.h"
#include "OpenGLRenderEngine/Base/Material.h"
#include "SegmentBufferManager.h"
#include "CriticalSectionLock.h"

// 间接绘制命令
struct IndirectDrawCommand {
	GLuint indexCount;					// 要绘制的索引数量
	GLuint instanceCount;				// 实例化数量（1 = 非实例化）
	GLuint indexfirst;					// EBO中的起始索引位置
	GLuint vertexFirst;					// VBO中的顶点偏移（顶点数，不是字节）
	GLuint baseInstanceIDFirst;			// base实例ID起始值
};

struct IndirectDrawMeta
{
	GLuint indexCount = 0;
	GLuint indexfirst = 0;
	GLuint vertexFirst = 0;
};

class VBOSegmentBuffer :public SegmentBufferBase
{
public:
	VBOSegmentBuffer();
	~VBOSegmentBuffer();
public:
	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length);
	GLuint GetVBO() const;
private:
	void Need();

private:
	unsigned int _VBO;
	uint64_t _size;
};

class EBOSegmentBuffer :public SegmentBufferBase
{
public:
	EBOSegmentBuffer();
	~EBOSegmentBuffer();
public:
	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length);
	GLuint GetEBO() const;
private:
	void Need();

private:
	unsigned int _EBO;
	uint64_t _size;
};

class SSBOSegmentBuffer :public SegmentBufferBase
{
public:
	SSBOSegmentBuffer();
	~SSBOSegmentBuffer();
public:
	virtual void ReSize(uint64_t length);
	virtual void WriteData(const void* mem, uint64_t first, uint64_t length);
	virtual void Memcpy(uint64_t destFirst, uint64_t srcFirst, uint64_t length);
	std::shared_ptr<SSBO> GetSSBO() const;
private:
	void Need();

private:
	std::shared_ptr<SSBO> _ssbo;
};

class IndirectDrawManager
{
public:
	static std::shared_ptr<IndirectDrawManager> Instance();

	// Mesh相关
	void setupMesh(Mesh& mesh);
	void deleteMesh(Mesh& mesh);
	bool GetIndirectDrawMeta(Mesh& mesh, IndirectDrawMeta& meta);
	GLuint GetVBO();
	GLuint GetEBO();

	// Material相关
	void setupMaterial(Material& material);
	void deleteMaterial(Material& material);
	bool GetMaterialIndex(Material& material, uint64_t& index);
	std::shared_ptr<SSBO> GetSSBO();

private:
	IndirectDrawManager();

private:
	SegmentBufferManager<VBOSegmentBuffer, std::string> _VBOManager;
	SegmentBufferManager<EBOSegmentBuffer, std::string> _EBOManager;
	CriticalSectionLock _meshMutex;

	SegmentBufferManager<SSBOSegmentBuffer, std::string> _MaterialManager;
	CriticalSectionLock _materialMutex;

	SegmentBufferManager<SSBOSegmentBuffer, std::string> _AnimatorManager;
	CriticalSectionLock _animatorMutex;
};

