#pragma once

#include <QtCore/qglobal.h>

#if defined(SENTRYQML_STATIC)
#    define SENTRYQML_EXPORT
#elif defined(SENTRYQML_LIBRARY)
#    define SENTRYQML_EXPORT Q_DECL_EXPORT
#else
#    define SENTRYQML_EXPORT Q_DECL_IMPORT
#endif
