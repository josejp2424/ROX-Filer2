/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef __ABOX_H__
/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#define __ABOX_H__

#include <gtk/gtk.h>

/* Public GObject cast/check helpers for the GTK3 widget subclass. */
#define ABOX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), abox_get_type(), ABox))
#define ABOX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), \
					abox_get_type(), ABoxClass))
#define IS_ABOX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), abox_get_type()))

typedef struct _ABoxClass  ABoxClass;
typedef struct _ABox ABox;

typedef enum
{
	ABOX_OPERATION_COPY = 0,
	ABOX_OPERATION_MOVE,
	ABOX_OPERATION_DELETE
} ABoxOperationKind;

struct _ABox
{
	GtkDialog 	parent_widget;

	GtkWidget	*quiet;
	GtkWidget	*flag_box;	/* HBox for flags */
	GtkWidget	*dir_label;	/* Shows what is being processed now */
	GtkWidget	*log;		/* The TextView for the messages */
	GtkWidget	*log_hbox;
	/* Agregado por josejp2424 (2026): control de detalles para mantener
	 * compactas las ventanas animadas de copia, movimiento y borrado. */
	GtkWidget	*details;
	gboolean	 compact_log;
	/* Rox-Filer2 2.12.2-25: compact Thunar-inspired operation status.
	 * A small document icon travels from source to destination/delete while
	 * the progress bar remains determinate and displays the real percentage. */
	GtkWidget	*operation_status_box;
	GtkWidget	*operation_animation;
	GtkWidget	*operation_source_icon;
	GtkWidget	*operation_flying_icon;
	GtkWidget	*operation_target_icon;
	GtkWidget	*operation_file_label;
	GtkWidget	*operation_from_label;
	GtkWidget	*operation_to_label;
	GtkWidget	*operation_remaining_label;
	guint		 operation_animation_id;
	gint		 operation_animation_x;
	ABoxOperationKind operation_kind;
	gint64		 progress_started_us;
	GtkWidget	*results;	/* List of filenames found */
	GtkWidget	*entry;		/* Plain entry, or part of combo */
	FilerWindow	*preview;

	GtkWidget       *cmp_area;      /* Area where files are compared */
	GtkWidget       *cmp_icon[2];
	GtkWidget       *cmp_name[2];
	GtkWidget       *cmp_size[2];
	GtkWidget       *cmp_date[2];
	GtkWidget       *cmp_arrow;

	/* Agregado por josejp2424 (2026): opción para aplicar una decisión
	 * a todos los conflictos restantes de la operación. */
	GtkWidget       *apply_all;

	GtkWidget       *progress;      /* Progress bar, NULL until set */

	gchar		*next_dir;	/* NULL => no timer active */
	gint		next_timer;

	gboolean	question;	/* Asking a question? */
};

struct _ABoxClass
{
	GtkDialogClass 	parent_class;

	void		(*flag_toggled)(ABox *abox, gint response);
	void		(*abort_operation)(ABox *abox);
};

GType	abox_get_type   		(void);
GtkWidget* abox_new			(const gchar *title, gboolean quiet);
GtkWidget *abox_add_flag		(ABox *abox,
					 const gchar *label,
					 const gchar *tip,
					 gint response,
					 gboolean default_value);
void	abox_ask			(ABox *abox,
					 const gchar *question);
void	abox_ask_conflict		(ABox *abox,
					 const gchar *question);
gboolean abox_apply_to_all		(ABox *abox);
void	abox_cancel_ask			(ABox *abox);
void	abox_set_current_object		(ABox *abox,
					 const gchar *message);
void	abox_log			(ABox *abox,
					 const gchar *message,
					 const gchar *style);
void	abox_add_results		(ABox *abox);
void	abox_add_filename		(ABox *abox,
					 const gchar *pathname);
void	abox_clear_results		(ABox *abox);
void	abox_add_combo			(ABox *abox,
					 const gchar *tlabel, 
					 GList *presets,
					 const gchar *text,
					 GtkWidget *help_button);
void	abox_add_entry			(ABox *abox,
					 const gchar *text,
					 GtkWidget *help_button);

void	abox_show_compare		(ABox *abox, gboolean show);
void	abox_set_file			(ABox *abox, int file,
					 const gchar *path);
void    abox_set_percentage             (ABox *abox, int per);
void    abox_set_operation_kind         (ABox *abox, ABoxOperationKind kind);
void    abox_set_operation_route        (ABox *abox, const gchar *source,
                                         const gchar *dest);
void    abox_set_operation_file         (ABox *abox, const gchar *path);
void    abox_start_operation_progress   (ABox *abox);
void    abox_stop_operation_progress    (ABox *abox);

#endif /* __ABOX_H__ */
