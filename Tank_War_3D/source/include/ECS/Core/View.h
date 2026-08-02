#pragma once

#include "Entity.h"
#include <vector>
#include <tuple>

template<typename... ComponentTypes>
class View {
public:
	View(const std::vector<Entity>& matchingEntities) : m_matchingEntities(matchingEntities) {}

	class Iterator {
	public:
		using value_type = std::tuple<Entity, ComponentTypes&...>;

		Iterator(typename std::vector<Entity>::const_iterator it) : m_it(it) {}

		value_type operator*() const {
			const Entity& entity = *m_it;
			return value_type(
				entity,
				entity.template getComponent<ComponentTypes>()...
			);
		}

		Iterator& operator++() {
			++m_it;
			return *this;
		}

		Iterator operator++(int) {
			Iterator temp = *this;
			++(*this);
			return temp;
		}

		bool operator==(const Iterator& other) const {
			return m_it == other.m_it;
		}

		bool operator!=(const Iterator& other) const {
			return !(*this == other);
		}

	private:
		typename std::vector<Entity>::const_iterator m_it;
	};

	Iterator begin() const {
		return Iterator(m_matchingEntities.begin());
	}

	Iterator end() const {
		return Iterator(m_matchingEntities.end());
	}

	// 便捷方法
	bool empty() const {
		return m_matchingEntities.empty();
	}

	size_t size() const {
		return m_matchingEntities.size();
	}

	const std::vector<Entity>& getEntities() const {
		return m_matchingEntities;
	}

private:
	std::vector<Entity> m_matchingEntities;
};