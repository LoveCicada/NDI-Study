add_executable(NDIReceiver
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

target_link_libraries(NDIReceiver PRIVATE ndi_study_common)
ndi_study_deploy_runtime(NDIReceiver)
