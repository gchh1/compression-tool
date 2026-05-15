/**
 * @file IAlgorithm.hpp
 * @author Algorithm interface
 * @brief
 * @version 0.1
 * @date 2026-04-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "BlockProfile.hpp"
#include "VizEvent.hpp"  // compressor::viz types + IVizObserver

#include <vector>

namespace compressor::algorithm {

/**
 * @brief
 *
 */
struct AlgorithmStatus {
    size_t bytes_consumed{0};
    size_t bytes_produced{0};

    bool need_input{false};
    bool need_output{false};

    bool done{false};
};

class IAlgorithm {
   public:
    virtual ~IAlgorithm() = default;

    virtual auto process(std::span<const uint8_t> read,
                         std::span<uint8_t> write, bool is_last_chunk)
        -> AlgorithmStatus = 0;

    virtual auto reset(void) -> void = 0;

    virtual auto getBlockProfile() -> std::optional<BlockProfile> {
        return std::nullopt;
    }
};

// Bring viz types into algorithm namespace for backward compatibility
using compressor::viz::BlockBoundary;
using compressor::viz::DPStateEvent;
using compressor::viz::HuffmanTreeBuilt;
using compressor::viz::IVizObserver;
using compressor::viz::MatchEvent;
using compressor::viz::VizEvent;

class AlgorithmBase : public IAlgorithm {
   public:
    virtual ~AlgorithmBase() = default;

    auto process(std::span<const uint8_t> read, std::span<uint8_t> write,
                 bool is_last_chunk) -> AlgorithmStatus final;

    auto reset(void) -> void {}

    void attachObserver(IVizObserver* obs) { observers_.push_back(obs); }
    void detachObserver(IVizObserver* obs) {
        observers_.erase(
            std::remove(observers_.begin(), observers_.end(), obs),
            observers_.end());
    }

   protected:
    utils::BitReader reader_;
    utils::BitWriter writer_;

    virtual auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk)
        -> void = 0;

    /// Send an event to all attached observers.  When no observers are
    /// attached the event is never constructed (zero overhead).
    template <typename E>
    void notifyObservers(E&& e) {
        if (observers_.empty()) return;
        VizEvent ev{std::forward<E>(e)};
        for (auto* obs : observers_) {
            obs->onEvent(ev);
        }
    }

    void notifyBlockFinish() {
        for (auto* obs : observers_) {
            obs->onBlockFinish();
        }
    }

    void notifyCompressionFinish() {
        for (auto* obs : observers_) {
            obs->onCompressionFinish();
        }
    }

   private:
    std::vector<IVizObserver*> observers_;
};

}  // namespace compressor::algorithm