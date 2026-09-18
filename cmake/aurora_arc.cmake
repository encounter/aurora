add_library(aurora_arc STATIC lib/revolution/arc.cpp)
add_library(aurora::arc ALIAS aurora_arc)
set_target_properties(aurora_arc PROPERTIES FOLDER "aurora")

target_link_libraries(aurora_arc PUBLIC aurora::core)
