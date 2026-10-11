#pragma once

#include "types.hpp"

#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace sema::data {

template <typename Record>
class catalog {
public:
    void add(Record record) {
        if (record.id == 0) throw std::runtime_error("catalog record ID cannot be zero");
        if (!records_.emplace(record.id, std::move(record)).second) {
            throw std::runtime_error("duplicate catalog record ID");
        }
    }

    void add_or_replace(Record record) {
        if (record.id == 0) throw std::runtime_error("catalog record ID cannot be zero");
        records_.insert_or_assign(record.id, std::move(record));
    }

    [[nodiscard]] const Record* find(const template_id id) const noexcept {
        const auto found = records_.find(id);
        return found == records_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] Record* find_mutable(const template_id id) noexcept {
        const auto found = records_.find(id);
        return found == records_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] const auto& records() const noexcept { return records_; }

private:
    std::unordered_map<template_id, Record> records_;
};

} // namespace sema::data
