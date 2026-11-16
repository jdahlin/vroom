/*
 * predefined.cpp: [ext.gnu] gnu_common_predefined_macros (GNU cpp manual
 * 3.7.2, Common Predefined Macros), whose replacement depends on where they
 * are used ([cpp.predefined]/4 lets an implementation predefine names that
 * "begin with a leading underscore followed by an uppercase letter or a
 * second underscore"):
 *
 *   __COUNTER__        0, 1, 2, ... at each use
 *   __INCLUDE_LEVEL__  how deep in #include the use is (0: the main file)
 *   __BASE_FILE__      the main file's name, as a string literal
 *   __TIMESTAMP__      the current file's modification time, "Sun Sep 16
 *                      01:03:52 1973"
 */
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "../../cplusplus-internal.h"

namespace cplusplus
{

void preprocessor::gnu_define_dynamic_macros()
{
    define_dynamic("__COUNTER__", dynamic_macro::counter, false);
    define_dynamic("__INCLUDE_LEVEL__", dynamic_macro::include_level, false);
    define_dynamic("__BASE_FILE__", dynamic_macro::base_file, false);
    define_dynamic("__TIMESTAMP__", dynamic_macro::timestamp, false);
}

bool preprocessor::gnu_dynamic_macro(const macro_definition &m,
                                     owned_buffer &text)
{
    switch (m.dynamic) {
    case dynamic_macro::counter:
        vbuffer_format_append(text.get(), "%u", counter++);
        return true;
    case dynamic_macro::include_level:
        vbuffer_format_append(text.get(), "%u",
                              (unsigned)(inclusions.size() - 1));
        return true;
    case dynamic_macro::base_file:
        if (main_file)
            string_literal_of(main_file->name.c_str(), text.get());
        else
            string_literal_of("", text.get());
        return true;
    case dynamic_macro::timestamp: {
        struct stat st;
        char stamp[64];
        if (stat(inclusions.back().file->name.c_str(), &st) == 0) {
            struct tm t;
            localtime_r(&st.st_mtime, &t);
            strftime(stamp, sizeof stamp, "\"%a %b %e %H:%M:%S %Y\"", &t);
        } else
            snprintf(stamp, sizeof stamp, "\"??? ??? ?? ??:??:?? ????\"");
        vbuffer_string_append(text.get(), stamp);
        return true;
    }
    default:
        return false;
    }
}

} // namespace cplusplus
