cmake_minimum_required(VERSION 3.28)

project(obs-live-dynamic-fx VERSION 0.1.0 LANGUAGES CXX)

# Point CMAKE_PREFIX_PATH at an OBS Studio development install (or a build of
# obs-studio) that exports the libobs CMake package.
find_package(libobs REQUIRED)

add_library(${PROJECT_NAME} MODULE)
target_sources(${PROJECT_NAME} PRIVATE
  src/plugin-main.cpp
  src/dynamic-visual-filter.cpp
  src/dynamic-audio-filter.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Mirrors OBS' normal plug-in layout.  The installation prefix may be the
# OBS Studio root for local testing, or any staging directory for packaging.
if(WIN32)
  set(OBS_PLUGIN_BIN_DESTINATION "obs-plugins/64bit")
else()
  set(OBS_PLUGIN_BIN_DESTINATION "obs-plugins")
endif()

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION "${OBS_PLUGIN_BIN_DESTINATION}"
  LIBRARY DESTINATION "${OBS_PLUGIN_BIN_DESTINATION}"
)
install(DIRECTORY data/
  DESTINATION "data/obs-plugins/${PROJECT_NAME}"
)
