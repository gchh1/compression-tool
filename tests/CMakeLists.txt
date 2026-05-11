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
