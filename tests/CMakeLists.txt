add_executable(
    test_Deflate
    test_Deflate.cpp
)

target_link_libraries(
    test_Deflate
    PRIVATE
    algorithm
)

add_executable(
    test_roundtrip
    test_roundtrip.cpp
)

target_link_libraries(
    test_roundtrip
    PRIVATE
    algorithm
)

add_executable(
    test_wcx_directory_archive
    test_wcx_directory_archive.cpp
)

target_link_libraries(
    test_wcx_directory_archive
    PRIVATE
    api
)

add_executable(
    test_PackWriter_RoundTrip
    test_PackWriter_RoundTrip.cpp
)

target_link_libraries(
    test_PackWriter_RoundTrip
    PRIVATE
    api
)

add_executable(
    test_PackWriter
    test_PackWriter.cpp
)

target_link_libraries(
    test_PackWriter
    PRIVATE
    archiver
)

add_executable(
    test_wcx_file_strict
    test_wcx_file_strict.cpp
)

target_link_libraries(
    test_wcx_file_strict
    PRIVATE
    api
)

add_executable(
    test_wcx_strict_no_compat
    test_wcx_strict_no_compat.cpp
)

target_link_libraries(
    test_wcx_strict_no_compat
    PRIVATE
    api
)

add_executable(
    test_wcx_corrupt
    test_wcx_corrupt.cpp
)

target_link_libraries(
    test_wcx_corrupt
    PRIVATE
    api
)

add_executable(
    test_lzss_noflag
    test_lzss_noflag.cpp
)

target_link_libraries(
    test_lzss_noflag
    PRIVATE
    api
)

add_executable(
    test_lzss_algorithm_new_matrix
    test_lzss_algorithm_new_matrix.cpp
)

target_link_libraries(
    test_lzss_algorithm_new_matrix
    PRIVATE
    api_new
    core_new
)

add_executable(
    test_deflate
    test_deflate.cpp
)

target_link_libraries(
    test_deflate
    PRIVATE
    api
)

add_executable(
    test_lzdp_memory_streaming_parity
    test_lzdp_memory_streaming_parity.cpp
)

target_link_libraries(
    test_lzdp_memory_streaming_parity
    PRIVATE
    api
)

add_executable(
    test_lzdp_dpflate_regression
    test_lzdp_dpflate_regression.cpp
)

target_link_libraries(
    test_lzdp_dpflate_regression
    PRIVATE
    algorithm
)

add_executable(
    test_lzdp_dpflate_stream
    test_lzdp_dpflate_stream.cpp
)

target_link_libraries(
    test_lzdp_dpflate_stream
    PRIVATE
    api
    algorithm
    processor
)

add_executable(
    test_HuffmanTree3HM
    test_HuffmanTree3HM.cpp
)

target_link_libraries(
    test_HuffmanTree3HM
    PRIVATE
    algorithm
)

add_executable(
    test_algorithm_comparison
    test_algorithm_comparison.cpp
)

target_link_libraries(
    test_algorithm_comparison
    PRIVATE
    api
)

add_executable(
    test_mixed_dpflate_only
    test_mixed_dpflate_only.cpp
)

target_link_libraries(
    test_mixed_dpflate_only
    PRIVATE
    core
)

add_executable(
    test_lzdp_memory_flag
    test_lzdp_memory_flag.cpp
)

target_link_libraries(
    test_lzdp_memory_flag
    PRIVATE
    core
)

add_executable(
    test_dpflate_3hm_stream
    test_dpflate_3hm_stream.cpp
)

target_link_libraries(
    test_dpflate_3hm_stream
    PRIVATE
    api
    algorithm
)

add_executable(
    test_dpflate_bin64k_3k_stream
    test_dpflate_bin64k_3k_stream.cpp
)

target_link_libraries(
    test_dpflate_bin64k_3k_stream
    PRIVATE
    algorithm
    processor
)

add_executable(
    test_dpflate_stream_trace
    test_dpflate_stream_trace.cpp
)

target_link_libraries(
    test_dpflate_stream_trace
    PRIVATE
    algorithm
    processor
)

add_executable(
    test_cartesian_stream_matrix
    test_cartesian_stream_matrix.cpp
)

target_link_libraries(
    test_cartesian_stream_matrix
    PRIVATE
    api
    algorithm
    processor
    core
)

add_executable(
    test_small_stream_7k_67k
    test_small_stream_7k_67k.cpp
)

target_link_libraries(
    test_small_stream_7k_67k
    PRIVATE
    api
    algorithm
    processor
    core
)

add_executable(
    test_lzdp_cartesian_stream_matrix
    test_lzdp_cartesian_stream_matrix.cpp
)

target_link_libraries(
    test_lzdp_cartesian_stream_matrix
    PRIVATE
    api
    algorithm
    processor
    core
)

add_executable(
    test_3hm_binary
    test_3hm_binary.cpp
)

target_link_libraries(
    test_3hm_binary
    PRIVATE
    api
)

add_executable(
    test_lzdp_binary
    test_lzdp_binary.cpp
)

target_link_libraries(
    test_lzdp_binary
    PRIVATE
    api
)

add_executable(
    test_3hm_all
    test_3hm_all.cpp
)

target_link_libraries(
    test_3hm_all
    PRIVATE
    api
)

add_executable(
    test_regression_baseline
    test_regression_baseline.cpp
)

target_link_libraries(
    test_regression_baseline
    PRIVATE
    api
)

add_executable(
    debug_dpflate
    debug_dpflate.cpp
)

target_link_libraries(
    debug_dpflate
    PRIVATE
    api
)

add_executable(
    debug_token_trace
    debug_token_trace.cpp
)

target_link_libraries(
    debug_token_trace
    PRIVATE
    algorithm
)

add_executable(
    test_algorithm_new_lzdp
    test_algorithm_new_lzdp.cpp
)

target_include_directories(
    test_algorithm_new_lzdp
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core_new/include
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_algorithm_new_lzdp
    PRIVATE
    core_new
    algorithm_new
)

add_executable(
    test_lzdp_imdb_gui_cfg
    test_lzdp_imdb_gui_cfg.cpp
)

target_include_directories(
    test_lzdp_imdb_gui_cfg
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core_new/include
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
    ${CMAKE_SOURCE_DIR}/src/api_new/include
)

target_link_libraries(
    test_lzdp_imdb_gui_cfg
    PRIVATE
    api_new
    core_new
    algorithm_new
)

add_executable(
    test_algorithm_new_phase1
    test_algorithm_new_phase1.cpp
)

target_include_directories(
    test_algorithm_new_phase1
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_algorithm_new_phase1
    PRIVATE
    algorithm_new
)

add_executable(
    test_algorithm_new_phase2
    test_algorithm_new_phase2.cpp
)

target_include_directories(
    test_algorithm_new_phase2
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_algorithm_new_phase2
    PRIVATE
    algorithm_new
)

add_executable(
    test_algorithm_new_lzss_deflate
    test_algorithm_new_lzss_deflate.cpp
)

target_include_directories(
    test_algorithm_new_lzss_deflate
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core_new/include
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_algorithm_new_lzss_deflate
    PRIVATE
    core_new
    algorithm_new
)

add_executable(
    test_lzss_roundtrip_debug
    test_lzss_roundtrip_debug.cpp
)

target_include_directories(
    test_lzss_roundtrip_debug
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_lzss_roundtrip_debug
    PRIVATE
    algorithm_new
)

add_executable(
    test_lzss_noflag_debug
    test_lzss_noflag_debug.cpp
)

target_include_directories(
    test_lzss_noflag_debug
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_lzss_noflag_debug
    PRIVATE
    algorithm_new
)

add_executable(
    test_lzss_noflag_text10k
    test_lzss_noflag_text10k.cpp
)

target_include_directories(
    test_lzss_noflag_text10k
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_lzss_noflag_text10k
    PRIVATE
    algorithm_new
)

add_executable(
    test_lzss_noflag_minimal
    test_lzss_noflag_minimal.cpp
)

target_include_directories(
    test_lzss_noflag_minimal
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_lzss_noflag_minimal
    PRIVATE
    algorithm_new
)

add_executable(
    test_lzss_noflag_sizes
    test_lzss_noflag_sizes.cpp
)

target_include_directories(
    test_lzss_noflag_sizes
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/algorithm_new/include
)

target_link_libraries(
    test_lzss_noflag_sizes
    PRIVATE
    algorithm_new
)

add_executable(
    test_deflate_huffman_minimal
    test_deflate_huffman_minimal.cpp
)

target_link_libraries(
    test_deflate_huffman_minimal
    PRIVATE
    algorithm_new
)

add_executable(
    test_deflate_3hm_large
    test_deflate_3hm_large.cpp
)

target_link_libraries(
    test_deflate_3hm_large
    PRIVATE
    algorithm_new
)

add_executable(
    test_lzdp_debug
    test_lzdp_debug.cpp
)

target_link_libraries(
    test_lzdp_debug
    PRIVATE
    algorithm_new
)
