#pragma once

#include <absl/strings/str_format.h>
#include <atomic>
#include <concepts>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

namespace am {

template <std::integral T>
struct MetricAverageValue {
	void add(T val) {
		sum_.fetch_add(val, std::memory_order_relaxed);
		count_.fetch_add(1, std::memory_order_relaxed);
	}
	void reset() {
		sum_.store(0, std::memory_order_relaxed);
		count_.store(0, std::memory_order_relaxed);
	}
	std::tuple<T, T, int> avg() const {
		T sum = sum_.load(std::memory_order_relaxed);
		int co = count_.load(std::memory_order_relaxed);
		T mean = co > 0 ? sum / co : 0;
		return std::make_tuple(mean, sum, co);
	}
	std::atomic<T> sum_{};
	std::atomic<int> count_{};

	template <typename Sink>
  	friend void AbslStringify(Sink &sink, const MetricAverageValue &metric) {
  		const auto &[avg, sm, co] = metric.avg();
    	absl::Format(&sink, "%v (%v; %v)", avg, sm, co);
  	}
};

template <std::integral T>
struct MetricSimpleValue {
	void add(T val) {
		val_.fetch_add(val, std::memory_order_relaxed);
	}
	void reset() {
		val_.store(0, std::memory_order_relaxed);
	}
	T value() const {
		return val_.load(std::memory_order_relaxed);
	}
	std::atomic<T> val_{};
	template <typename Sink>
  	friend void AbslStringify(Sink &sink, const MetricSimpleValue &metric) {
  		auto val = metric.val_.load(std::memory_order_relaxed);
    	absl::Format(&sink, "%v", val);
  	}
};

template<class>
inline constexpr bool always_false_v = false;

template <std::integral T>
struct Metric {

	using Variants = std::variant<MetricAverageValue<T>, MetricSimpleValue<T>>;
	std::string name_;
	Variants val_;
	void add(T val) {
		std::visit([&val](auto&& arg) {
			using V = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<V, MetricAverageValue<T>>) {
				arg.add(val);
			} else if constexpr (std::is_same_v<V, MetricSimpleValue<T>>) {
				arg.add(val);
			} else {
				static_assert(always_false_v<V>, "non-exhaustive visitor!");
			}
		}, val_);
	}
	void reset() {
		std::visit([](auto&& arg) {
			using V = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<V, MetricAverageValue<T>>) {
				arg.reset();
			} else if constexpr (std::is_same_v<V, MetricSimpleValue<T>>) {
				arg.reset();
			} else {
				static_assert(always_false_v<V>, "non-exhaustive visitor!");
			}
		}, val_);
	}
	template <typename Sink>
  	friend void AbslStringify(Sink &sink, const Metric &metric) {
		std::visit([&sink, &metric](auto&& arg) {
			using V = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<V, MetricAverageValue<T>>) {
				absl::Format(&sink, "metric: %s %v", metric.name_, std::get<MetricAverageValue<T>>(metric.val_));
			} else if constexpr (std::is_same_v<V, MetricSimpleValue<T>>) {
				absl::Format(&sink, "metric: %s %v", metric.name_, std::get<MetricSimpleValue<T>>(metric.val_));
			} else {
				static_assert(always_false_v<V>, "non-exhaustive visitor!");
			}
		}, metric.val_);
  	}
  	static Metric<T> create_counter(std::string_view name) {
		return {std::string(name), Variants{std::in_place_type<MetricSimpleValue<T>>}};
	};

	static Metric<T> create_average(std::string_view name) {
		return {
			std::string(name), Variants{std::in_place_type<MetricAverageValue<T>>}};
	};
};

} // namespace am
