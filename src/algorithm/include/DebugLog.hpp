#pragma once

#if defined(DEBUG_LOG_ENABLED) && DEBUG_LOG_ENABLED
#include <cstdio>
#define DEBUG_LOG(fmt, ...) \
    do { std::fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define DEBUG_LOG(fmt, ...) ((void)0)
#endif