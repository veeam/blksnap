#ifndef VERSION_H_
#define VERSION_H_

// these values are sed'ed so they may be empty

#define FILEVER_MAJOR 5
#define FILEVER_MINOR 0
#define FILEVER_REVISION 0
#define FILEVER_BUILD 0

#if (FILEVER_MAJOR+0) == 0
#undef FILEVER_MAJOR
#define FILEVER_MAJOR 0
#endif

#if (FILEVER_MINOR+0) == 0
#undef FILEVER_MINOR
#define FILEVER_MINOR 0
#endif

#if (FILEVER_REVISION+0) == 0
#undef FILEVER_REVISION
#define FILEVER_REVISION 0
#endif

#if (FILEVER_BUILD+0) == 0
#undef FILEVER_BUILD
#define FILEVER_BUILD 0
#endif

#define _VERSION_STRING(maj,min,rev,build) #maj "." #min "." #rev "." #build
#define VERSION_STRING(maj,min,rev,build) _VERSION_STRING(maj,min,rev,build)

#define FILEVER_STR VERSION_STRING(FILEVER_MAJOR, FILEVER_MINOR, FILEVER_REVISION, FILEVER_BUILD)

#endif /* VERSION_H_ */
