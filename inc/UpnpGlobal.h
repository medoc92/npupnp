#ifndef UPNPGLOBAL_H
#define UPNPGLOBAL_H

/*!
 * \file
 *
 * \brief Defines constants that for some reason are not defined on some systems.
 */

#ifdef _WIN32
    /*
     * EXPORT_SPEC
     */
    #ifdef UPNP_STATIC_LIB
        #define EXPORT_SPEC
    #else /* UPNP_STATIC_LIB */
        #ifdef LIBUPNP_EXPORTS
            /*! set up declspec for dll export to make functions
             * visible to library users */
            #define EXPORT_SPEC __declspec(dllexport)
        #else /* LIBUPNP_EXPORTS */
            #define EXPORT_SPEC __declspec(dllimport)
        #endif /* LIBUPNP_EXPORTS */
    #endif /* UPNP_STATIC_LIB */

    #ifdef UPNP_USE_MSVCPP
        /* define some things the M$ VC++ doesn't know */
        #define UPNP_INLINE _inline
        typedef __int64 int64_t;
        #define PRId64 "I64d"
        /* no ssize_t defined for VC */
        #ifdef  _WIN64
        typedef int64_t ssize_t;
        #else
        typedef int32_t ssize_t;
        #endif
    #endif /* UPNP_USE_MSVCPP */

    #ifdef __GNUC__
        #define UPNP_INLINE inline
    #endif /* __GNUC__ */
#else
    /*! Export functions on WIN32 DLLs. */
    #define EXPORT_SPEC

    /*!
     * \brief Declares an inline function.
     *
     * Surprisingly, there are some compilers that do not understand the
     * inline keyword. This definition makes the use of this keyword
     * portable to these systems.
     */
    #ifdef __STRICT_ANSI__
        #define UPNP_INLINE __inline__
    #else
        #define UPNP_INLINE inline
    #endif
#endif

/* Sized integer types. */
#include <stdint.h>

#endif /* UPNPGLOBAL_H */
