#pragma once

#include <unordered_map>
#include <algorithm>
#include <vector>
#include <functional>
#include "CriticalSectionLock.h"

// 双向映射容器

template <typename L, typename R, typename Lockable = CriticalSectionLock>
class SafeBiDirectionalMap
{

public:
    // 插入双向映射
    bool Insert(const L &left, const R &right)
    {
        LockGuard guard(mutex);

        // 检查左键是否已存在
        if (ContainsLeft(left))
        {
            return false; // 左键已存在
        }

        // 检查右键是否已存在
        if (ContainsRight(right))
        {
            return false; // 右键已存在
        }
        l_to_r[left] = right;
        r_to_l[right] = left;

        return true;
    }

    // 插入或替换（确保左右唯一性）
    void InsertOrUpdate(const L &left, const R &right)
    {
        LockGuard guard(mutex);

        // 如果左键已存在，删除旧的映射
        auto left_it = l_to_r.find(left);
        if (left_it != l_to_r.end())
        {
            R old_right = left_it->second;
            r_to_l.erase(old_right);
            l_to_r.erase(left_it);
        }

        // 如果右键已存在，删除旧的映射
        auto right_it = r_to_l.find(right);
        if (right_it != r_to_l.end())
        {
            L old_left = right_it->second;
            l_to_r.erase(old_left);
            r_to_l.erase(right_it);
        }

        // 现在可以安全插入
        l_to_r[left] = right;
        r_to_l[right] = left;
    }

    // 通过左键查找右键
    bool FindByLeft(const L &left, R &right)
    {
        LockGuard guard(mutex);

        auto it = l_to_r.find(left);
        if (it != l_to_r.end())
        {
            right = it->second;
            return true;
        }
        return false;
    }

    // 通过右键查找左键
    bool FindByRight(const R &right, L &left)
    {
        LockGuard guard(mutex);

        auto it = r_to_l.find(right);
        if (it != r_to_l.end())
        {
            left = it->second;
            return true;
        }
        return false;
    }

    // 删除（通过左键）
    bool EraseByLeft(const L &left)
    {
        LockGuard guard(mutex);

        auto it = l_to_r.find(left);
        if (it == l_to_r.end())
            return false; // 左键不存在

        R rightvalue = std::move(it->second);
        l_to_r.erase(it);
        r_to_l.erase(rightvalue);
        return true;
    }

    // 删除（通过右键）
    bool EraseByRight(const R &right)
    {
        LockGuard guard(mutex);

        auto it = r_to_l.find(right);
        if (it == r_to_l.end())
            return false; // 右键不存在

        L leftvalue = std::move(it->second);
        r_to_l.erase(it);
        l_to_r.erase(leftvalue);
        return true;
    }

    // 检查是否存在左键
    bool ContainsLeft(const L &left) const
    {
        LockGuard guard(mutex);
        return l_to_r.find(left) != l_to_r.end();
    }

    // 检查是否存在右键
    bool ContainsRight(const R &right) const
    {
        LockGuard guard(mutex);
        return r_to_l.find(right) != r_to_l.end();
    }

    // 获取所有左键
    std::vector<L> GetAllLefts() const
    {
        LockGuard guard(mutex);
        std::vector<L> result;
        result.reserve(l_to_r.size());
        for (const auto &pair : l_to_r)
        {
            result.push_back(pair.first);
        }
        return result;
    }

    // 获取所有右键
    std::vector<R> GetAllRights() const
    {
        LockGuard guard(mutex);
        std::vector<R> result;
        result.reserve(r_to_l.size());
        for (const auto &pair : r_to_l)
        {
            result.push_back(pair.first);
        }
        return result;
    }

    // 清空
    void Clear()
    {
        LockGuard guard(mutex);
        l_to_r.clear();
        r_to_l.clear();
    }

    // 大小
    size_t Size() const
    {
        LockGuard guard(mutex);
        return l_to_r.size();
    }

    // 是否为空
    bool Empty() const
    {
        LockGuard guard(mutex);
        return l_to_r.empty();
    }

    void EnsureCall(std::function<void(std::unordered_map<L, R> &l_to_r, std::unordered_map<R, L> r_to_l)> callback)
    {
        if (callback)
        {
            LockGuard guard(mutex);
            callback(l_to_r, r_to_l);
        }
    }

    void Lock()
    {
        mutex.Enter();
    }

    void UnLock()
    {
        mutex.Leave();
    }

    auto MakeLockGuard()
    {
        return LockGuard(mutex);
    }

private:
    std::unordered_map<L, R> l_to_r; // 左到右映射
    std::unordered_map<R, L> r_to_l; // 右到左映射
    mutable Lockable mutex;
};
