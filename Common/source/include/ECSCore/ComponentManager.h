#pragma once

#include <memory>
#include <unordered_map>
#include <typeindex>
#include <iostream>
#include <string>
#include <stdexcept>
#include <limits>
#include "Types.h"
#include "ComponentTypes.h"
#include "ComponentArray.h"
#include "CriticalSectionLock.h"

using ComponentMask = std::bitset<MAX_COMPONENTS>;

class ComponentManager
{
private:
    // 存储所有组件数组的映射
    std::unordered_map<ComponentTypeID, std::shared_ptr<void>> componentArrays;

    // 类型信息映射
    std::unordered_map<ComponentTypeID, EcsTypeIdentity> typeIdentities;
    std::unordered_map<ComponentTypeID, ComponentSlotID> componentSlots;

    mutable SharedLock componentArraysMutex;
    mutable SharedLock typeIdentitiesMutex;
    mutable SharedLock componentSlotsMutex;

public:
    template<typename T>
    ComponentSlotID registerComponent() {
        ComponentTypeID typeId = ComponentType<T>::getId();
        const EcsTypeIdentity identity = GetEcsTypeIdentity<T>();

        {
            SharedLockGuard guard_componentSlots(componentSlotsMutex);
            auto existing = componentSlots.find(typeId);
            if (existing != componentSlots.end()) {
                SharedLockGuard guard_typeIdentities(typeIdentitiesMutex);
                const auto& registered = typeIdentities.at(typeId);
                if (registered.stableName != identity.stableName || registered.nativeName != identity.nativeName)
                    throw std::runtime_error("ECS component type identity collision: " + identity.stableName + " (" + identity.nativeName + ") conflicts with " + registered.stableName + " (" + registered.nativeName + ")");
                return existing->second;
            }
        }

        {
            LockGuard guard_componentSlots(componentSlotsMutex);
            LockGuard guard_typeIdentities(typeIdentitiesMutex);
            LockGuard guard_componentArrays(componentArraysMutex);
            if (componentSlots.size() >= MAX_COMPONENTS)
                throw std::runtime_error("Maximum number of component types exceeded");
            ComponentSlotID slot = static_cast<ComponentSlotID>(componentSlots.size());
            componentSlots.emplace(typeId, slot);
            componentArrays[typeId] = std::make_shared<ComponentArray<T>>();
            typeIdentities[typeId] = identity;
            return slot;
        }
    }

    template<typename T>
    ComponentSlotID getComponentSlot() { return registerComponent<T>(); }

    bool tryGetComponentSlot(ComponentTypeID typeId, ComponentSlotID& slot) const {
        SharedLockGuard guard_componentSlots(componentSlotsMutex);
        auto it = componentSlots.find(typeId);
        if (it == componentSlots.end()) return false;
        slot = it->second;
        return true;
    }

    template<typename T>
    inline std::shared_ptr<ComponentArray<T>> TryGetComponentArray(ComponentTypeID typeId)
    {
        SharedLockGuard guard_componentArrays(componentArraysMutex);
        auto it = componentArrays.find(typeId);
        if (it == componentArrays.end())
            return nullptr;
        return std::static_pointer_cast<ComponentArray<T>>(it->second);
    }

    template<typename T, typename... Args>
    T& addComponent(const Entity& entity, Args&&... args) {
        ComponentTypeID typeId = ComponentType<T>::getId();
        registerComponent<T>();
        auto array = TryGetComponentArray<T>(typeId);
        return array->addComponent(entity, std::forward<Args>(args)...);
    }

    template<typename T>
    void removeComponent(const Entity& entity)
    {
        ComponentTypeID typeId = ComponentType<T>::getId();
        auto array = TryGetComponentArray<T>(typeId);
        assert(array && "ComponentArray do not exist!");
        array->removeComponent(entity);
    }

    template<typename T>
    T& getComponent(const EntityID& entityId)
    {
        ComponentTypeID typeId = ComponentType<T>::getId();
        auto array = TryGetComponentArray<T>(typeId);
        assert(array && "ComponentArray do not exist!");
        return array->getComponent(entityId);
    }

    template<typename T>
    T* tryGetComponent(const EntityID& entityId)
    {
        ComponentTypeID typeId = ComponentType<T>::getId();

        auto array = TryGetComponentArray<T>(typeId);
        return array ? array->tryGetComponent(entityId) : nullptr;
    }

    template<typename T>
    bool hasComponent(const EntityID& entityId) const
    {
        ComponentTypeID typeId = ComponentType<T>::getId();

        auto array = TryGetComponentArray<T>(typeId);
        return array && array->hasComponent(entityId);
    }

    void entityDestroyed(const Entity& entity)
    {
        SharedLockGuard guard_componentArrays(componentArraysMutex);
        for (auto& pair : componentArrays)
        {
            auto array = std::static_pointer_cast<IComponentArray>(pair.second);
            array->removeComponent(entity);
        }
    }

    void printRegisteredComponents() const {
        std::cout << "Registered Components:\n";
        SharedLockGuard guard_typeIdentities(typeIdentitiesMutex);
        for (const auto& pair : typeIdentities) {
            std::cout << "  ID: " << pair.first << " -> " << pair.second.stableName << "\n";
        }
    }

    IComponent* tryGetComponentByID(const EntityID& entityId, ComponentTypeID typeId)
    {
        std::shared_ptr<IComponentArray> array;
        {
            SharedLockGuard guard_componentArrays(componentArraysMutex);
            auto it = componentArrays.find(typeId);
            if (it == componentArrays.end())
                return nullptr;
            array = std::static_pointer_cast<IComponentArray>(it->second);
        }

        return array ? array->tryGetComponentAsIComponent(entityId) : nullptr;
    }

    bool removeComponentByID(const Entity& entity, ComponentTypeID typeId)
    {
        std::shared_ptr<IComponentArray> array;
        {
            SharedLockGuard guard_componentArrays(componentArraysMutex);
            auto it = componentArrays.find(typeId);
            if (it == componentArrays.end())
                return false;
            array = std::static_pointer_cast<IComponentArray>(it->second);
        }
        if (!array || !array->hasComponent(entity.getId()))
            return false;
        array->removeComponent(entity);
        return true;
    }

    void DuplicateAllComponent(Entity& newEntity, const Entity& srcEntity, std::vector<ComponentSlotID>& outComponentIds)
    {
        SharedLockGuard guard_componentArrays(componentArraysMutex);
        SharedLockGuard guard_componentSlots(componentSlotsMutex);
        for (auto& pair : componentArrays)
        {
            auto array = std::static_pointer_cast<IComponentArray>(pair.second);
            if (array->duplicateComponent(newEntity, srcEntity))
                outComponentIds.push_back(componentSlots.at(pair.first));
        }
    }
};
