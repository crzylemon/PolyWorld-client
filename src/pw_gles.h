/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: pw_gles.h                                                                           |
|   Purpose: GLES vs desktop GL                                                               |
\*-------------------------------------------------------------------------------------------*/

#ifndef PW_GLES_H
#define PW_GLES_H

#if defined(PW_GLES) || defined(__EMSCRIPTEN__) || defined(PW_IOS)
#define PW_USE_GLES 1
#else
#define PW_USE_GLES 0
#endif

#if defined(__ANDROID__) || defined(PW_IOS)
#define PW_MOBILE 1
#else
#define PW_MOBILE 0
#endif

#endif
