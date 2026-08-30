#ifndef ROX_FILER_PAIR_H
#define ROX_FILER_PAIR_H

#include "filer.h"

void filer_pair_init(void);
gboolean filer_pair_is_enabled(void);
void filer_pair_open(FilerWindow *source, const gchar *left_path,
                     const gchar *right_path);
void filer_pair_realign(void);
void filer_pair_startup_if_enabled(void);

#endif
