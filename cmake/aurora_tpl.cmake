add_library(aurora_tpl STATIC lib/revolution/tpl.cpp)
add_library(aurora::tpl ALIAS aurora_tpl)
set_target_properties(aurora_tpl PROPERTIES FOLDER "aurora")

target_link_libraries(aurora_tpl PUBLIC aurora::core)
