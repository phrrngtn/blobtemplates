#include <stdio.h>
#include <stdlib.h>

#include "blobtemplates.h"

int main(void) {
    /* basic render */
    const char *tmpl = "Hello {{ name }}! You have {{ count }} messages.";
    const char *data = "{\"name\": \"Alice\", \"count\": 5}";

    char *result = blobtemplates_render(tmpl, data);
    if (!result) {
        fprintf(stderr, "Error: %s\n", blobtemplates_errmsg());
        return 1;
    }
    printf("Basic:   %s\n", result);
    blobtemplates_free(result);

    /* render with custom delimiters */
    const char *tmpl2 = "Server=<< host >>;Port=<< port >>;";
    const char *data2 = "{\"host\": \"localhost\", \"port\": 5432}";
    const char *opts  = "{\"expression\": [\"<<\", \">>\"]}";

    result = blobtemplates_render_with_options(tmpl2, data2, opts);
    if (!result) {
        fprintf(stderr, "Error: %s\n", blobtemplates_errmsg());
        return 1;
    }
    printf("Custom:  %s\n", result);
    blobtemplates_free(result);

    /* array iteration */
    const char *tmpl3 = "{% for item in items %}{{ item }}, {% endfor %}done.";
    const char *data3 = "{\"items\": [\"apple\", \"banana\", \"cherry\"]}";

    result = blobtemplates_render(tmpl3, data3);
    if (!result) {
        fprintf(stderr, "Error: %s\n", blobtemplates_errmsg());
        return 1;
    }
    printf("Loop:    %s\n", result);
    blobtemplates_free(result);

    return 0;
}
