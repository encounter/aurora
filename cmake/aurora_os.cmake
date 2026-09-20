add_library(aurora_os STATIC
	lib/dolphin/os/OSInit.cpp
	lib/dolphin/os/OSCache.cpp
	lib/dolphin/os/OSBootInfo.cpp
	lib/dolphin/os/OSTime.cpp
	lib/dolphin/os/OSAlloc.cpp
	lib/dolphin/os/OSReport.cpp
)

if (AURORA_TARGET_RVL)
	target_sources(aurora_os PRIVATE
		lib/revolution/os/OSMemory.cpp
		lib/revolution/os/internal.hpp
		lib/revolution/os/OSArena.cpp
		lib/revolution/os/OSAddress.cpp
		lib/revolution/aralt.cpp
	)
	target_compile_definitions(aurora_os PUBLIC RVL)
else ()
	target_sources(aurora_os PRIVATE
		lib/dolphin/os/OSMemory.cpp
		lib/dolphin/os/internal.hpp
		lib/dolphin/os/OSArena.cpp
		lib/dolphin/os/OSAddress.cpp
		lib/dolphin/AR.cpp
	)
endif ()

add_library(aurora::os ALIAS aurora_os)
set_target_properties(aurora_os PROPERTIES FOLDER "aurora")

target_include_directories(aurora_os PUBLIC include)
target_link_libraries(aurora_os PRIVATE aurora::core)
