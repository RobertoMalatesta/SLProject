# 
# CMake top-level configuration of SLProject
# 

cmake_minimum_required(VERSION 3.3 FATAL_ERROR)

set(SL_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Register general cmake commands (set_policy, source_group_by_path, list_extract)
include(cmake/CustomCommands.cmake)

# The FOLDER properties are needed in Visual Studio and XCode generated projects for nested folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "")

set(META_PROJECT_NAME "SLProject")
set(SL_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Declare project
project(${META_PROJECT_NAME} C CXX)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

include(cmake/SetGitBranchNameAndCommitID.cmake)
include(cmake/CompileOptions.cmake)
include(cmake/DownloadPrebuilts.cmake)

add_subdirectory(apps)
add_subdirectory(externals)
add_subdirectory(lib-SLProject)
