#pragma once
#include <cstdint>

// Force the compiler to pack the struct exactly as it appears in memory (1-byte alignment).
// This prevents padding, allowing us to cast raw network/file bytes directly into this struct.
#pragma pack(push, 1)

// Simulated NASDAQ ITCH 5.0 "Add Order" Message (Type 'A')
// Total Size: 36 bytes per message
struct ItchAddOrder {
    char message_type;             // Always 'A' for Add Order
    uint16_t stock_locate;         // Stock identifier
    uint16_t tracking_number;      // Internal system number
    uint64_t timestamp;            // Nanoseconds since midnight
    uint64_t order_reference_num;  // Unique ID
    char side;                     // 'B' for Buy, 'S' for Sell
    uint32_t shares;               // Quantity
    char stock[8];                 // Ticker symbol (e.g., "AAPL    ")
    uint32_t price;                // Integer price
};

#pragma pack(pop)