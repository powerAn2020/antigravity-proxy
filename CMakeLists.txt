cmake_minimum_required(VERSION 3.10)

project(antigravity-proxy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(STATIC_RUNTIME "使用静态运行库(/MT)，减少运行库依赖" ON)

if(MSVC)
    add_compile_options(/utf-8)
    if(STATIC_RUNTIME)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")
    endif()
    add_compile_definitions(NOMINMAX)
endif()

include(CMakePackageConfigHelpers)

if(WIN32)
  set(MINHOOK_MAJOR_VERSION 1)
  set(MINHOOK_MINOR_VERSION 3)
  set(MINHOOK_PATCH_VERSION 3)
  set(MINHOOK_VERSION ${MINHOOK_MAJOR_VERSION}.${MINHOOK_MINOR_VERSION}.${MINHOOK_PATCH_VERSION})

  option(BUILD_SHARED_LIBS "build shared version" OFF)

  # MinHook Sources
  set(SOURCES_MINHOOK 
    "src/buffer.c"
    "src/hook.c"
    "src/trampoline.c"
  )

  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SOURCES_HDE "src/hde/hde64.c")
  else()
    set(SOURCES_HDE "src/hde/hde32.c")
  endif()

  # Proxy Sources
  set(SOURCES_PROXY
    "src/main.cpp"
    "src/hooks/Hooks.cpp"
    "src/proxy/VersionProxy.cpp"
    "src/update/UpdateChecker.cpp"
  )

  # Resources
  if(BUILD_SHARED_LIBS)
    set(RESOURCES 
      "dll_resources/MinHook.rc"
      "dll_resources/MinHook.def"
    )
  endif()

  # Build antigravity-proxy DLL (输出名为 version.dll)
  add_library(version SHARED ${SOURCES_MINHOOK} ${SOURCES_HDE} ${SOURCES_PROXY} ${RESOURCES})

  if(MSVC)
    set_target_properties(version PROPERTIES
      LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/version.def\""
    )
  else()
    target_link_options(version PRIVATE
      "-Wl,${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/version.def"
    )
  endif()

  target_include_directories(version PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
    $<INSTALL_INTERFACE:include>
  )
  target_include_directories(version PRIVATE "src/")
  target_include_directories(version PRIVATE "src/hde/")

  target_link_libraries(version PRIVATE ws2_32 winhttp)

  if(NOT MSVC AND STATIC_RUNTIME)
    target_link_options(version PRIVATE -static-libstdc++ -static-libgcc)
  endif()

  # Build Antigravity CLI shim (输出名为 dbghelp.dll)
  add_library(dbghelp SHARED
    "src/proxy/DbgHelpShim.cpp"
  )

  if(MSVC)
    set_target_properties(dbghelp PROPERTIES
      LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/dbghelp.def\""
    )
  else()
    target_link_options(dbghelp PRIVATE
      "-Wl,${CMAKE_CURRENT_SOURCE_DIR}/src/proxy/dbghelp.def"
    )
  endif()

  add_dependencies(dbghelp version)
  add_custom_command(TARGET dbghelp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:version>
            $<TARGET_FILE_DIR:dbghelp>/antigravity_proxy.dll
    COMMENT "复制 Antigravity CLI 代理主体"
  )

  set_target_properties(version PROPERTIES PREFIX "")
  set_target_properties(dbghelp PROPERTIES PREFIX "")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)   
    set_target_properties(version PROPERTIES OUTPUT_NAME "version")
    set_target_properties(dbghelp PROPERTIES OUTPUT_NAME "dbghelp")
    set_target_properties(version PROPERTIES DEBUG_POSTFIX ".x64d")
    set_target_properties(dbghelp PROPERTIES DEBUG_POSTFIX ".x64d")
    set_target_properties(version PROPERTIES RELEASE_POSTFIX "")
    set_target_properties(dbghelp PROPERTIES RELEASE_POSTFIX "")
  else()
    set_target_properties(version PROPERTIES OUTPUT_NAME "version")
    set_target_properties(dbghelp PROPERTIES OUTPUT_NAME "dbghelp")
    set_target_properties(version PROPERTIES DEBUG_POSTFIX ".x32d")
    set_target_properties(dbghelp PROPERTIES DEBUG_POSTFIX ".x32d")
    set_target_properties(version PROPERTIES RELEASE_POSTFIX "")
    set_target_properties(dbghelp PROPERTIES RELEASE_POSTFIX "")
  endif()

  install(TARGETS version dbghelp RUNTIME DESTINATION "bin")

else()
  # ========================================================
  # Linux Build: libantigravity_proxy.so + antigravity-proxy CLI
  # ========================================================
  find_package(Threads REQUIRED)

  add_library(antigravity_proxy SHARED
    "src/main_linux.cpp"
    "src/hooks/LinuxHooks.cpp"
    "src/update/UpdateChecker.cpp"
  )

  target_include_directories(antigravity_proxy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
    $<INSTALL_INTERFACE:include>
  )
  target_include_directories(antigravity_proxy PRIVATE "src/")

  target_link_libraries(antigravity_proxy PRIVATE
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )

  set_target_properties(antigravity_proxy PROPERTIES
    OUTPUT_NAME "antigravity_proxy"
    PREFIX "lib"
  )

  # CLI 启动与管理工具
  add_executable(antigravity-proxy
    "src/cli/main.cpp"
  )

  target_include_directories(antigravity-proxy PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )

  target_link_libraries(antigravity-proxy PRIVATE
    ${CMAKE_DL_LIBS}
    Threads::Threads
  )

  install(TARGETS antigravity_proxy antigravity-proxy
    LIBRARY DESTINATION "lib"
    RUNTIME DESTINATION "bin"
  )

endif()

# ========================================================
# TESTS
# ========================================================
option(BUILD_TESTS "构建单元测试（默认开启）" ON)
if(BUILD_TESTS)
  enable_testing()

  add_executable(antigravity_tests
    "tests/test_ipv6_parser.cpp"
  )
  target_include_directories(antigravity_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  if(WIN32)
    target_link_libraries(antigravity_tests PRIVATE ws2_32)
  endif()
  add_test(NAME antigravity_tests COMMAND antigravity_tests)

  add_executable(process_name_tests
    "tests/test_process_name.cpp"
  )
  target_include_directories(process_name_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  add_test(NAME process_name_tests COMMAND process_name_tests)

  add_executable(udp_policy_tests
    "tests/test_udp_policy.cpp"
  )
  target_include_directories(udp_policy_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  add_test(NAME udp_policy_tests COMMAND udp_policy_tests)

  add_executable(socket_family_tests
    "tests/test_socket_family.cpp"
  )
  target_include_directories(socket_family_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  if(WIN32)
    target_link_libraries(socket_family_tests PRIVATE ws2_32)
  endif()
  add_test(NAME socket_family_tests COMMAND socket_family_tests)

  add_executable(local_target_policy_tests
    "tests/test_local_target_policy.cpp"
  )
  target_include_directories(local_target_policy_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  if(WIN32)
    target_link_libraries(local_target_policy_tests PRIVATE ws2_32)
  endif()
  add_test(NAME local_target_policy_tests COMMAND local_target_policy_tests)

  if(WIN32)
    add_executable(dbghelp_shim_tests
      "tests/test_dbghelp_shim.cpp"
    )
    set_target_properties(dbghelp_shim_tests PROPERTIES OUTPUT_NAME "agy")
    add_dependencies(dbghelp_shim_tests dbghelp)
    add_test(
      NAME dbghelp_shim_tests
      COMMAND dbghelp_shim_tests "$<TARGET_FILE:dbghelp>"
    )
  else()
    add_executable(linux_hooks_tests
      "tests/test_linux_hooks.cpp"
    )
    target_include_directories(linux_hooks_tests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_link_libraries(linux_hooks_tests PRIVATE
      ${CMAKE_DL_LIBS}
      Threads::Threads
    )
    add_test(NAME linux_hooks_tests COMMAND linux_hooks_tests)
  endif()
endif()
