add_library(ndi_study_common STATIC
    ndi/NdiContext.cpp
    ndi/NdiFinder.cpp
    ndi/NdiSender.cpp
    ndi/NdiReceiver.cpp
    capture/DxgiScreenCapture.cpp
    capture/AlphaTestPattern.cpp
    capture/VideoFormatConvert.cpp
    encode/MfH264Encoder.cpp
    render/Dx11VideoRenderer.cpp
    render/VideoPreviewWidget.cpp
    audio/WasapiLoopbackCapture.cpp
    audio/SdlAudioPlayer.cpp
)

target_include_directories(ndi_study_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/ndi
        ${CMAKE_CURRENT_SOURCE_DIR}/capture
        ${CMAKE_CURRENT_SOURCE_DIR}/encode
        ${CMAKE_CURRENT_SOURCE_DIR}/render
        ${CMAKE_CURRENT_SOURCE_DIR}/audio
)

target_link_libraries(ndi_study_common
    PUBLIC
        NDI::Lib
        Qt5::Core
        Qt5::Widgets
        d3d11
        dxgi
        d3dcompiler
        mfplat
        mfuuid
        mfreadwrite
        wmcodecdspuuid
        ole32
        mmdevapi
)

if(NDI_STUDY_HAS_SDL2)
    target_include_directories(ndi_study_common PUBLIC ${SDL2_INCLUDE_DIR})
    target_link_libraries(ndi_study_common PUBLIC ${SDL2_LIBRARY})
    target_compile_definitions(ndi_study_common PUBLIC NDI_STUDY_HAS_SDL2=1)
endif()

target_compile_definitions(ndi_study_common PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
