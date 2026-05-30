if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG} CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${CONFIG}")
endforeach()

function(ndi_study_deploy_runtime target_name)
    if(NDI_DLL_PATH)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NDI_DLL_PATH}"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMENT "Deploy NDI runtime for ${target_name}")
    endif()
    if(NDI_STUDY_HAS_SDL2 AND SDL2_DLL)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_DLL}"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMENT "Deploy SDL2 for ${target_name}")
    endif()
endfunction()
