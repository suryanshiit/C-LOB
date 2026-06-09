#pragma once
#include "PriceLevel.hpp"
#include <array>
#include <algorithm>

// Assuming a bounded price range for absolute O(1) array lookups (e.g., 0 to 100,000 ticks)
constexpr uint32_t MAX_PRICE = 100000;

class LimitBook {
private:
    std::unique_ptr<std::array<PriceLevel, MAX_PRICE>> m_bids;
    std::unique_ptr<std::array<PriceLevel, MAX_PRICE>> m_asks;
    
    // Track top of book
    uint32_t m_best_bid{0};
    uint32_t NO_VALID_ASK = MAX_PRICE - 1; // 99999, a valid sentinel
    uint32_t m_best_ask{NO_VALID_ASK};

    OrderMemoryPool& m_pool; // Reference to our Phase 1 memory allocator

public:
    explicit LimitBook(OrderMemoryPool& pool) : m_pool(pool) {}

    // Main entry point for new orders
    void process_order(Order* incoming) {
        if (incoming->side == Side::Buy) {
            match_logic<Side::Buy>(incoming);
        } else {
            match_logic<Side::Sell>(incoming);
        }
    }

private:
    // Compile-time specialized matching engine
    template <Side S>
    void match_logic(Order* incoming) {
        auto& maker_book = (S == Side::Buy) ? *m_asks : *m_bids;
        auto& best_maker_price = (S == Side::Buy) ? m_best_ask : m_best_bid;

        // 1. Cross the spread
        while (incoming->quantity > 0) {
            // Check if marketable
            bool is_marketable;
            if constexpr (S == Side::Buy) {
                is_marketable = (best_maker_price <= incoming->price);
            } else {
                is_marketable = (best_maker_price >= incoming->price) && (best_maker_price != 0);
            }

            if (!is_marketable || maker_book[best_maker_price].is_empty()) {
                break; // No more matching possible
            }

            PriceLevel& level = maker_book[best_maker_price];
            Order* resting_order = level.head;

            // 2. Execute against resting order
            uint32_t fill_qty = std::min(incoming->quantity, resting_order->quantity);
            
            incoming->quantity -= fill_qty;
            resting_order->quantity -= fill_qty;
            level.volume -= fill_qty;

            // 3. Handle resting order depletion
            if (resting_order->quantity == 0) {
                level.remove(resting_order);
                m_pool.deallocate(resting_order); // Recycle memory instantly
            }

            // 4. Update Top of Book if level is depleted
            if (level.is_empty()) {
                if constexpr (S == Side::Buy) {
                    // Ask depleted, scan up for next best ask
                    while (best_maker_price < MAX_PRICE && maker_book[best_maker_price].is_empty()) {
                        best_maker_price++;
                    }
                } else {
                    // Bid depleted, scan down for next best bid
                    while (best_maker_price > 0 && maker_book[best_maker_price].is_empty()) {
                        best_maker_price--;
                    }
                }
            }
        }

        // 5. Add remaining quantity to the book
        if (incoming->quantity > 0) {
            add_to_book<S>(incoming);
        } else {
            m_pool.deallocate(incoming); // Order fully filled, recycle
        }
    }

    template <Side S>
    void add_to_book(Order* order) {
        if constexpr (S == Side::Buy) {
            (*m_bids)[order->price].append(order);
            if (order->price > m_best_bid) {
                m_best_bid = order->price;
            }
        } else {
            (*m_asks)[order->price].append(order);
            if (order->price < m_best_ask) {
                m_best_ask = order->price;
            }
        }
    }
};