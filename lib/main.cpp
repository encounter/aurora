#include <aurora/main.h>
#undef main

#include <SDL3/SDL_main.h>

int main(int argc, char** argv) { return aurora_main(argc, argv); }
