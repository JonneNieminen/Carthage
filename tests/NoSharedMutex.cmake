# Fails if Carthage's own code uses std::shared_mutex. Tracktion Engine takes shared_mutex locks on the
# audio thread (tests/rtsan/baseline.txt), and inlined lock calls are attributed to the innermost frame
# that owns them; keeping Carthage off shared_mutex entirely leaves no room for confusion.
file(GLOB_RECURSE sources "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h" "${SOURCE_DIR}/tools/*.cpp")
foreach(f IN LISTS sources)
    file(READ "${f}" text)
    if(text MATCHES "shared_mutex|shared_lock")
        message(FATAL_ERROR "${f} uses std::shared_mutex or std::shared_lock")
    endif()
endforeach()
