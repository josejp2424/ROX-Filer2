#ifndef ROX_SEARCH_INTEGRATION_H
#define ROX_SEARCH_INTEGRATION_H

#include "filer.h"

void search_integration_init(void);
gboolean search_integration_enabled(void);
gboolean search_integration_toolbar_enabled(void);
gboolean search_integration_available(FilerWindow *filer_window);
void search_integration_launch(FilerWindow *filer_window);

#endif
