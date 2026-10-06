#ifndef REAL_VERSION_NUMBER_H
#define REAL_VERSION_NUMBER_H

// The version of REAL in one place: the program (AppVersion.h) and the version
// resource of the executable (res/real-app.rc) take it from here. The build
// workflow checks that the text below, CMakeLists.txt, res/app.manifest and the
// tag of a release carry the same numbers. Plain macros only: the resource
// compiler reads this file as well.
#define REAL_VERSION_MAJOR 1
#define REAL_VERSION_MINOR 0
#define REAL_VERSION_PATCH 1
#define REAL_VERSION_TEXT "1.0.1"

#endif
