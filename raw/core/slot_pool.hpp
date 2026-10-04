#pragma once
// A table of objects addressed by (index, generation). Erasing a slot bumps its
// generation, so a handle kept past erase() no longer resolves, and a reused
// slot never answers to an old handle. Generation 0 is never issued.
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
namespace raw {
template<class T> class SlotPool {
public:
    struct Id { uint32_t index{0}, gen{0}; };
    Id insert(T value){
        uint32_t i;
        if (!free_.empty()){ i = free_.back(); free_.pop_back(); }
        else { i = (uint32_t)slots_.size(); slots_.emplace_back(); }
        Slot& s = slots_[i];
        s.value = std::move(value);
        return {i, s.gen};
    }
    T* get(uint32_t index, uint32_t gen){
        if (index >= slots_.size() || gen == 0 || slots_[index].gen != gen || !slots_[index].value) return nullptr;
        return &*slots_[index].value;
    }
    // Returns the object so the caller can release what it owns.
    std::optional<T> erase(uint32_t index, uint32_t gen){
        if (!get(index, gen)) return std::nullopt;
        Slot& s = slots_[index];
        std::optional<T> out = std::move(s.value);
        s.value.reset();
        if (++s.gen == 0) s.gen = 1;
        free_.push_back(index);
        return out;
    }
    std::size_t live() const {
        std::size_t n = 0;
        for (const Slot& s : slots_) n += s.value.has_value();
        return n;
    }
private:
    struct Slot { std::optional<T> value; uint32_t gen{1}; };
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
};
}
