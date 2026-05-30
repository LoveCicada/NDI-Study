add_executable(NDISender
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

target_link_libraries(NDISender PRIVATE ndi_study_common)
ndi_study_deploy_runtime(NDISender)
