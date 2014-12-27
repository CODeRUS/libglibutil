/*
 * Copyright (C) 2014-2015 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of the Jolla Ltd nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gutil_log.h"

#if defined(DEBUG) && defined(_WIN32)
#  include <windows.h>
#endif

#ifndef GLOG_SYSLOG
#  ifdef unix
#    define GLOG_SYSLOG 1
#  else
#    define GLOG_SYSLOG 0
#  endif
#endif /* GLOG_SYSLOG */

#ifndef GLOG_GLIB
#  define GLOG_GLIB 1
#endif /* GLOG_GLIB */

/* Allows timestamps in stdout log */
gboolean gutil_log_timestamp = TRUE;

/* Log configuration */
GLogProc gutil_log_func = gutil_log_stdout;
GLogModule gutil_log_default = {
    NULL,               /* name      */
    NULL,               /* parent    */
    NULL,               /* reserved  */
    GLOG_LEVEL_MAX,     /* max_level */
    GLOG_LEVEL_DEFAULT, /* level     */
    0                   /* flags     */
};

/* Log level descriptions */
static const struct _gutil_log_level {
    const char* name;
    const char* description;
} gutil_log_levels [] = {
    { "none",    "Disable log output" },
    { "error",   "Errors only"},
    { "warning", "From warning level to errors" },
    { "info",    "From information level to errors" },
    { "debug",   "From debug messages to errors" },
    { "verbose", "From verbose trace messages to errors" }
};

const char GLOG_TYPE_STDOUT[] = "stdout";
const char GLOG_TYPE_STDERR[] = "stderr";
const char GLOG_TYPE_CUSTOM[] = "custom";
#if GLOG_GLIB
const char GLOG_TYPE_GLIB[]   = "glib";
#endif
#if GLOG_SYSLOG
const char GLOG_TYPE_SYSLOG[] = "syslog";
#endif

G_STATIC_ASSERT(G_N_ELEMENTS(gutil_log_levels) > GLOG_LEVEL_MAX);
G_STATIC_ASSERT(G_N_ELEMENTS(gutil_log_levels) > GLOG_LEVEL_DEFAULT);

/**
 * Formats the string into the given buffer (most commonly allocated
 * on caller's stack). If formatted string fits into the provided buffer,
 * returns pointer to the given buffer. If formatted string is too long
 * to fit into the provided buffer, allocates a new one and returns pointer
 * to it.
 */
static
char*
gutil_log_format(
    char* buf,
    int bufsize,
    const char* format,
    va_list va)
{
    int size, nchars = -1;
    char* buffer;
    if (buf) {
        size = bufsize;
        buffer = buf;
    } else {
        size = MAX(100,bufsize);
        buffer = g_malloc(size);
    }
    while (buffer) {

        /* Try to print in the allocated space. */
        va_list va2;
        G_VA_COPY(va2, va);
        nchars = g_vsnprintf(buffer, size, format, va2);
        va_end(va2);

        /* Return the string or try again with more space. */
        if (nchars >= 0) {
            if (nchars < size) break;
            size = nchars+1;  /* Precisely what is needed */
        } else {
            size *= 2;        /* Twice the old size */
        }

        if (buffer != buf) g_free(buffer);
        buffer = g_malloc(size);
    }

    return buffer;
}

/* Forward output to stdout or stderr */
void
gutil_log_stdio(
    FILE* out,
    const char* name,
    int level,
    const char* format,
    va_list va)
{
    char t[32];
    char buf[512];
    const char* prefix = "";
    char* msg;
    if (gutil_log_timestamp) {
        time_t now;
        time(&now);
        strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S ", localtime(&now));
    } else {
        t[0] = 0;
    }
    switch (level) {
    case GLOG_LEVEL_WARN: prefix = "WARNING: "; break;
    case GLOG_LEVEL_ERR:  prefix = "ERROR: ";   break;
    default:              break;
    }
    msg = gutil_log_format(buf, sizeof(buf), format, va);
#if defined(DEBUG) && defined(_WIN32)
    {
        char s[1023];
        if (name) {
            g_snprintf(s, sizeof(s), "%s[%s] %s%s\n", t, name, prefix, msg);
        } else {
            g_snprintf(s, sizeof(s), "%s%s%s\n", t, prefix, msg);
        }
        OutputDebugStringA(s);
    }
#endif
    if (name) {
        fprintf(out, "%s[%s] %s%s\n", t, name, prefix, msg);
    } else {
        fprintf(out, "%s%s%s\n", t, prefix, msg);
    }
    if (msg != buf) g_free(msg);
}

void
gutil_log_stdout(
    const char* name,
    int level,
    const char* format,
    va_list va)
{
    gutil_log_stdio(stdout, name, level, format, va);
}

void
gutil_log_stderr(
    const char* name,
    int level,
    const char* format,
    va_list va)
{
    gutil_log_stdio(stderr, name, level, format, va);
}

/* Formards output to syslog */
#if GLOG_SYSLOG
#include <syslog.h>
void
gutil_log_syslog(
    const char* name,
    int level,
    const char* format,
    va_list va)
{
    int priority;
    const char* prefix = NULL;
    switch (level) {
    default:
    case GLOG_LEVEL_INFO:
        priority = LOG_NOTICE;
        break;
    case GLOG_LEVEL_VERBOSE:
        priority = LOG_DEBUG;
        break;
    case GLOG_LEVEL_DEBUG:
        priority = LOG_INFO;
        break;
    case GLOG_LEVEL_WARN:
        priority = LOG_WARNING;
        prefix = "WARNING! ";
        break;
    case GLOG_LEVEL_ERR:
        priority = LOG_ERR;
        prefix = "ERROR! ";
        break;
    }
    if (name || prefix) {
        char buf[512];
        g_vsnprintf(buf, sizeof(buf), format, va);
        if (!prefix) prefix = "";
        if (name) {
            syslog(priority, "[%s] %s%s", name, prefix, buf);
        } else {
            syslog(priority, "%s%s", prefix, buf);
        }
    } else {
        vsyslog(priority, format, va);
    }
}
#endif /* GLOG_SYSLOG */

/* Forwards output to g_logv */
#if GLOG_GLIB
void
gutil_log_glib(
    const char* name,
    int level,
    const char* format,
    va_list va)
{
    GLogLevelFlags flags;
    switch (level) {
    default:
    case GLOG_LEVEL_INFO:    flags = G_LOG_LEVEL_MESSAGE;  break;
    case GLOG_LEVEL_VERBOSE: flags = G_LOG_LEVEL_DEBUG;    break;
    case GLOG_LEVEL_DEBUG:   flags = G_LOG_LEVEL_INFO;     break;
    case GLOG_LEVEL_WARN:    flags = G_LOG_LEVEL_WARNING;  break;
    case GLOG_LEVEL_ERR:     flags = G_LOG_LEVEL_CRITICAL; break;
    }
    g_logv(name, flags, format, va);
}
#endif /* GLOG_GLIB */

/* Logging function */
void
gutil_logv_r(
    const GLogModule* module,
    int level,
    const char* prefix,
    const char* format,
    va_list va)
{
    if (module->parent && module->level == GLOG_LEVEL_INHERIT) {
        gutil_logv_r(module->parent, level, prefix, format, va);
    } else {
        const int max_level = (module->level == GLOG_LEVEL_INHERIT) ?
            gutil_log_default.level : module->level;
        if (level > GLOG_LEVEL_NONE && level <= max_level) {
            GLogProc log = gutil_log_func;
            if (G_LIKELY(log)) {
                log(prefix, level, format, va);
            }
        }
    }
}

void
gutil_logv(
    const GLogModule* module,
    int level,
    const char* format,
    va_list va)
{
    if (level != GLOG_LEVEL_NONE && gutil_log_func) {
        if (!module) module = &gutil_log_default;
        if (G_UNLIKELY(level == GLOG_LEVEL_ALWAYS)) {
            GLogProc log = gutil_log_func;
            if (G_LIKELY(log)) {
                if (level == GLOG_LEVEL_ALWAYS) {
                    log(module->name, level, format, va);
                }
            }
        } else {
            gutil_logv_r(module, level, module->name, format, va);
        }
    }
}

void
gutil_log(
    const GLogModule* module,
    int level,
    const char* format,
    ...)
{
    va_list va;
    va_start(va, format);
    gutil_logv(module, level, format, va);
    va_end(va);
}

void
gutil_log_assert(
    const GLogModule* module,
    int level,
    const char* expr,
    const char* file,
    int line)
{
    gutil_log(module, level, "Assert %s failed at %s:%d\r", expr, file, line);
}

/**
 * Check if logging is enabled for the specified log level
 */
static
gboolean
gutil_log_enabled_r(
    const GLogModule* module,
    int level)
{
    if (module->parent && module->level == GLOG_LEVEL_INHERIT) {
        return gutil_log_enabled_r(module->parent, level);
    } else {
        const int max_level = (module->level == GLOG_LEVEL_INHERIT) ?
            gutil_log_default.level : module->level;
        return (level > GLOG_LEVEL_NONE && level <= max_level);
    }
}

gboolean
gutil_log_enabled(
    const GLogModule* module,
    int level)
{
    if (gutil_log_func) {
        if (level == GLOG_LEVEL_ALWAYS) {
            return TRUE;
        } else if (level > GLOG_LEVEL_NONE) {
            if (!module) module = &gutil_log_default;
            return gutil_log_enabled_r(module, level);
        }
    }
    return FALSE;
}

/* gutil_log_parse_option helper */
static
int
gutil_log_parse_level(
    const char* str,
    GError** error)
{
    if (str && str[0]) {
        guint i;
        const size_t len = strlen(str);
        if (len == 1) {
            const char* valid_numbers = "012345";
            const char* number = strchr(valid_numbers, str[0]);
            if (number) {
                return number - valid_numbers;
            }
        }

        for (i=0; i<G_N_ELEMENTS(gutil_log_levels); i++) {
            if (!strncmp(gutil_log_levels[i].name, str, len)) {
                return i;
            }
        }
    }
    if (error) {
        *error = g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
            "Invalid log level '%s'", str);
    }
    return -1;
}

/**
 * Command line parsing helper. Option format is [module:]level where level
 * can be either a number or log level name ("none", "error", etc.)
 */
gboolean
gutil_log_parse_option(
    const char* opt,
    GLogModule** modules,
    int count,
    GError** error)
{
    const char* sep = strchr(opt, ':');
    if (sep) {
        const int modlevel = gutil_log_parse_level(sep+1, error);
        if (modlevel >= 0) {
            int i;
            const size_t namelen = sep - opt;
            for (i=0; i<count; i++) {
                if (!g_ascii_strncasecmp(modules[i]->name, opt, namelen) &&
                    strlen(modules[i]->name) == namelen) {
                    modules[i]->level = modlevel;
                    return TRUE;
                }
            }
            if (error) {
                *error = g_error_new(G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                    "Unknown log module '%.*s'", (int)namelen, opt);
            }
        }
    } else {
        const int deflevel = gutil_log_parse_level(opt, error);
        if (deflevel >= 0) {
            gutil_log_default.level = deflevel;
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Generates the string containg description of log levels and list of
 * log modules. The caller must deallocate the string with g_free
 */
char*
gutil_log_description(
    GLogModule** modules,   /* Known modules */
    int count)              /* Number of known modules */
{
    int i;
    GString* desc = g_string_sized_new(128);
    g_string_append(desc, "Log Levels:\n");
    for (i=0; i<=GLOG_LEVEL_VERBOSE; i++) {
        g_string_append_printf(desc, "   %d, ", i);
        g_string_append_printf(desc, "%-8s    ", gutil_log_levels[i].name);
        g_string_append(desc, gutil_log_levels[i].description);
        if (i == GLOG_LEVEL_DEFAULT) g_string_append(desc, " (default)");
        g_string_append(desc, "\n");
    }
    if (modules) {
        g_string_append(desc, "\nLog Modules:\n");
        for (i=0; i<count; i++) {
            g_string_append_printf(desc, "  %s\n", modules[i]->name);
        }
    }
    return g_string_free(desc, FALSE);
}

gboolean
gutil_log_set_type(
    const char* type,
    const char* default_name)
{
#if GLOG_SYSLOG
    if (!g_ascii_strcasecmp(type, GLOG_TYPE_SYSLOG)) {
        if (gutil_log_func != gutil_log_syslog) {
            openlog(NULL, LOG_PID | LOG_CONS, LOG_USER);
        }
        gutil_log_default.name = NULL;
        gutil_log_func = gutil_log_syslog;
        return TRUE;
    }
    if (gutil_log_func == gutil_log_syslog) {
        closelog();
    }
#endif /* GLOG_SYSLOG */
    gutil_log_default.name = default_name;
    if (!g_ascii_strcasecmp(type, GLOG_TYPE_STDOUT)) {
        gutil_log_func = gutil_log_stdout;
        return TRUE;
    } if (!g_ascii_strcasecmp(type, GLOG_TYPE_STDERR)) {
        gutil_log_func = gutil_log_stderr;
        return TRUE;
#if GLOG_GLIB
    } else if (!g_ascii_strcasecmp(type, GLOG_TYPE_GLIB)) {
        gutil_log_func = gutil_log_glib;
        return TRUE;
#endif /* GLOG_GLIB */
    }
    return FALSE;
}

const char*
gutil_log_get_type()
{
    return (gutil_log_func == gutil_log_stdout) ? GLOG_TYPE_STDOUT :
           (gutil_log_func == gutil_log_stderr) ? GLOG_TYPE_STDERR :
#if GLOG_SYSLOG
           (gutil_log_func == gutil_log_syslog) ? GLOG_TYPE_SYSLOG :
#endif /* GLOG_SYSLOG */
#if GLOG_GLIB
           (gutil_log_func == gutil_log_glib)   ? GLOG_TYPE_GLIB :
#endif /* GLOG_GLIB */
                                                  GLOG_TYPE_CUSTOM;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */