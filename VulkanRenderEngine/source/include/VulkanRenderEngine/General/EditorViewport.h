#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

// 编辑器视口 <-> 3D 渲染器的通道（和 DDGIDebugChannel 一样：渲染器持有，编辑器通过 VKRenderer3D::GetEditorViewportChannel 取）。
//   - 拾取：编辑器提交请求，渲染器在下一次场景渲染图里读回实体 ID 缓冲（GeometryPass 的编辑器附件）、发布结果，编辑器每帧来取
//   - 选中：编辑器每帧推送选中集合，渲染器在帧派发时取一份快照（EditorFrameSelection），画选中遮罩和描边
// 线程安全。

// ───────────── 拾取 ─────────────

// 拾取请求：坐标都是渲染目标像素（左上角为原点），不是视口 / 窗口坐标
struct EditorPickRequest
{
	uint64_t   id = 0;					// 编辑器分配，递增
	glm::uvec2 targetSize{ 0u };		// 发请求时的渲染目标尺寸；和读回那一帧的尺寸不一致就作废（valid = false）
	glm::ivec2 rectMin{ 0 };			// 读回的矩形（闭区间），渲染器会截到画面范围内
	glm::ivec2 rectMax{ 0 };
	glm::ivec2 focus{ 0 };				// 点选：鼠标所在像素
	bool       isBox = false;			// true = 框选（矩形内所有实体），false = 点选（focus 处最近的实体）
	bool       wantPosition = false;	// 点选时顺带读回命中像素的世界坐标（gPosition），拖放摆放物体用；框选忽略
};

struct EditorPickResult
{
	uint64_t requestId = 0;
	uint32_t frameIndex = 0;
	bool     valid = false;				// false：尺寸变了、矩形在画面外、读回失败
	uint32_t focusEntity = UINT32_MAX;	// 点选：focus 处的实体；focus 处没有时取矩形里离它最近的；都没有为 UINT32_MAX
	// 点选且 wantPosition 时：focusEntity 那个像素的世界坐标（物体表面）。focusEntity 为 UINT32_MAX 时没有
	bool      hasPosition = false;
	glm::vec3 focusPosition{ 0.0f };
	std::vector<uint32_t> entities;		// 框选：矩形内出现过的实体（去重、升序）
};

// ───────────── 选中显示 ─────────────

// 描边样式（颜色是最终画面上的颜色，已经过色调映射 / gamma）
struct EditorSelectionStyle
{
	glm::vec4 primaryColor{ 1.00f, 0.63f, 0.16f, 1.0f };		// 主选中
	glm::vec4 secondaryColor{ 1.00f, 0.82f, 0.35f, 1.0f };	// 其它选中（多选时）
	float widthPx = 2.0f;			// 描边宽度（渲染目标像素），着色器里限制在 1 ~ 6
	float occludedAlpha = 0.35f;	// 被挡住部分的描边透明度
	float occludedFill = 0.10f;		// 被挡住部分的内部叠色强度（0 = 不叠）
};

// 选中集合（实体 id，升序去重）
struct EditorSelection
{
	std::vector<uint32_t> ids;
	uint32_t primary = UINT32_MAX;		// 主选中（不在 ids 里时不区分颜色）
	uint64_t version = 0;

	bool Empty() const { return ids.empty(); }
	bool Contains(uint32_t id) const { return std::binary_search(ids.begin(), ids.end(), id); }
};

// 一帧用的选中快照：帧派发时（VKRenderer3D::OnDispatch）建一份挂到 RenderState::editorSelection 上，
// 遮罩 Pass 和描边 Pass 读同一份。maskDrawn：遮罩 Pass 这一帧真的画了东西（没画就不做描边）
struct EditorFrameSelection
{
	std::shared_ptr<const EditorSelection> selection;
	EditorSelectionStyle style;
	std::atomic<bool> maskDrawn{ false };
};

class EditorViewportChannel
{
public:
	// ── 拾取 ──

	// 编辑器：提交拾取请求。只保留最新的一个，还没被渲染器取走的旧请求直接作废
	void SubmitPick(const EditorPickRequest& request)
	{
		std::lock_guard lock(_mutex);
		_pendingPick = request;
	}

	// 编辑器：放弃等待（超时等）。还没被取走的请求撤掉，已经发布的结果丢掉
	void CancelPick(uint64_t requestId)
	{
		std::lock_guard lock(_mutex);
		if (_pendingPick && _pendingPick->id == requestId)
			_pendingPick.reset();
		if (_pickResult && _pickResult->requestId == requestId)
			_pickResult.reset();
	}

	// 渲染器：场景渲染图的 FrameBegin 里取走请求（取走后同一个请求不会再被下一帧处理）
	std::optional<EditorPickRequest> TakePick()
	{
		std::lock_guard lock(_mutex);
		std::optional<EditorPickRequest> request = std::move(_pendingPick);
		_pendingPick.reset();
		return request;
	}

	// 渲染器：发布结果（覆盖之前没被取走的结果）
	void PublishPick(EditorPickResult result)
	{
		std::lock_guard lock(_mutex);
		_pickResult = std::move(result);
	}

	// 编辑器：取 requestId 对应的结果（取走）。还没好、或者是别的请求的结果时返回空
	std::optional<EditorPickResult> PollPick(uint64_t requestId)
	{
		std::lock_guard lock(_mutex);
		if (!_pickResult || _pickResult->requestId != requestId)
			return std::nullopt;
		std::optional<EditorPickResult> result = std::move(_pickResult);
		_pickResult.reset();
		return result;
	}

	// ── 选中 ──

	// 编辑器：每帧推送都可以，内容没变时不做任何事（不分配、版本号不变）
	void SetSelection(std::vector<uint32_t> ids, uint32_t primary)
	{
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		ids.erase(std::remove(ids.begin(), ids.end(), UINT32_MAX), ids.end());

		std::lock_guard lock(_mutex);
		if (_selection && _selection->ids == ids && _selection->primary == primary)
			return;
		auto selection = std::make_shared<EditorSelection>();
		selection->ids = std::move(ids);
		selection->primary = primary;
		selection->version = ++_selectionVersion;
		_selection = std::move(selection);
	}

	// 渲染器：当前选中（可能为空指针 = 没选东西）
	std::shared_ptr<const EditorSelection> GetSelection() const
	{
		std::lock_guard lock(_mutex);
		return _selection;
	}

	void SetStyle(const EditorSelectionStyle& style)
	{
		std::lock_guard lock(_mutex);
		_style = style;
	}

	EditorSelectionStyle GetStyle() const
	{
		std::lock_guard lock(_mutex);
		return _style;
	}

private:
	mutable std::mutex _mutex;
	std::optional<EditorPickRequest> _pendingPick;
	std::optional<EditorPickResult> _pickResult;

	std::shared_ptr<const EditorSelection> _selection;
	uint64_t _selectionVersion = 0;
	EditorSelectionStyle _style;
};
