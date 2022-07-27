#ifndef AURORA_MAIN_H
#define AURORA_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

int aurora_main(int argc, char* argv[]);

#ifdef __cplusplus
}
#endif

#define main aurora_main

#endif
