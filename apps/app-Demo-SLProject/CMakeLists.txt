if("${CMAKE_SYSTEM_NAME}" MATCHES "Android")
    set(IDE_FOLDER "Android")
    add_subdirectory(android/app)
else()
    add_subdirectory(GLFW)
endif()
