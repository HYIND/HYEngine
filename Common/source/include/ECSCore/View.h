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
		using iterator_category = std::random_access_iterator_tag;
		using value_type = std::tuple<Entity, ComponentTypes&...>;
		using difference_type = std::ptrdiff_t;

		Iterator(typename std::vector<Entity>::const_iterator it) : m_it(it) {}

		value_type operator*() const {
			const Entity& entity = *m_it;
			return value_type(
				entity,
				entity.template getComponent<ComponentTypes>()...
			);
		}

		Iterator& operator++() { ++m_it; return *this; }
		Iterator operator++(int) { Iterator temp = *this; ++(*this); return temp; }

		Iterator& operator--() { --m_it; return *this; }
		Iterator operator--(int) { Iterator temp = *this; --(*this); return temp; }

		Iterator& operator+=(difference_type n) { m_it += n; return *this; }
		Iterator& operator-=(difference_type n) { m_it -= n; return *this; }

		Iterator operator+(difference_type n) const {
			Iterator result = *this;
			result += n;
			return result;
		}

		Iterator operator-(difference_type n) const {
			Iterator result = *this;
			result -= n;
			return result;
		}

		difference_type operator-(const Iterator& other) const {
			return m_it - other.m_it;
		}

		bool operator==(const Iterator& other) const { return m_it == other.m_it; }
		bool operator!=(const Iterator& other) const { return !(*this == other); }
		bool operator<(const Iterator& other) const { return m_it < other.m_it; }
		bool operator>(const Iterator& other) const { return m_it > other.m_it; }
		bool operator<=(const Iterator& other) const { return m_it <= other.m_it; }
		bool operator>=(const Iterator& other) const { return m_it >= other.m_it; }

		value_type operator[](difference_type n) const {
			return *(*this + n);
		}

	private:
		typename std::vector<Entity>::const_iterator m_it;
	};

	Iterator begin() const { return Iterator(m_matchingEntities.begin()); }
	Iterator end() const { return Iterator(m_matchingEntities.end()); }

	auto getComponents(size_t index) const {
		const Entity& entity = m_matchingEntities[index];
		return std::tuple<ComponentTypes&...>(
			entity.template getComponent<ComponentTypes>()...
		);
	}

	auto operator[](size_t index) const {
		const Entity& entity = m_matchingEntities[index];
		return std::tuple<Entity, ComponentTypes&...>(
			entity,
			entity.template getComponent<ComponentTypes>()...
		);
	}

	auto at(size_t index) const {
		if (index >= m_matchingEntities.size()) {
			throw std::out_of_range("View::at: index out of range");
		}
		return (*this)[index];
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