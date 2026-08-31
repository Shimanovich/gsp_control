#ifndef GSP_CONTROL_GLOBAL_H
#define GSP_CONTROL_GLOBAL_H

#include <QtCore/qglobal.h>

/*
 * GSP_CONTROL_APP      — сборка как приложение (отладка)
 * GSP_CONTROL_LIBRARY  — сборка как shared-библиотека
 * иначе                — хост импортирует символы из dll
 */
#if defined(GSP_CONTROL_APP)
#  define GSP_CONTROL_EXPORT
#elif defined(GSP_CONTROL_LIBRARY)
#  define GSP_CONTROL_EXPORT Q_DECL_EXPORT
#else
#  define GSP_CONTROL_EXPORT Q_DECL_IMPORT
#endif

#endif // GSP_CONTROL_GLOBAL_H
