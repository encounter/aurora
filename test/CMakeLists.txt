
add_executable(cardtest
    main.cpp)

set(PLAT_LIBS)
if (UNIX AND NOT APPLE)
    list(APPEND PLAT_LIBS rt)
endif()

target_link_libraries(cardtest
    kabufuda ${PLAT_LIBS})
