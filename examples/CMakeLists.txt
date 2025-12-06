add_executable(simple simple.c)
target_link_libraries(simple PRIVATE aurora::core aurora::gx aurora::main)
