/*
 * Rox-Filer2 interface style selection.
 */

#ifndef _INTERFACE_STYLE_H
#define _INTERFACE_STYLE_H

typedef enum {
	ROX_INTERFACE_CLASSIC = 0,
	ROX_INTERFACE_MODERN = 1
} RoxInterfaceStyle;

void interface_style_init(void);
void interface_style_set_command_override(RoxInterfaceStyle style);
void interface_style_clear_command_override(void);
RoxInterfaceStyle interface_style_current(void);
gboolean interface_style_is_modern(void);
const gchar *interface_style_name(void);

#endif /* _INTERFACE_STYLE_H */
