#include "config.h"
#include "global.h"
#include <gio/gio.h>
#include "drives_monitor.h"
#include "i18n.h"

/* 2.12.2-82: el sondeo era de 8 s, y cada ciclo hace fork/exec de lsblk (mas
 * blkid por dispositivo en la ruta sysfs de respaldo).  Eran unos 10.800
 * procesos por dia en un escritorio ocioso, y blkid despierta la lectora
 * optica.  GVolumeMonitor ya cubre los eventos reales; el sondeo queda solo
 * como red de seguridad para cambios que GIO no notifica. */
#define DRIVE_POLL_SECONDS 45

typedef struct {
    GWeakRef owner;
    RoxDrivesMonitorFunc callback;
    gpointer user_data;
} RoxDrivesSubscriber;

typedef struct {
    GObject *owner;
    RoxDrivesMonitorFunc callback;
    gpointer user_data;
} RoxDrivesNotifyTarget;

static GPtrArray *subscribers;
static GPtrArray *snapshot;
static GVolumeMonitor *volume_monitor;
static guint poll_source;
static gboolean scan_running;
static gboolean scan_again;
static gboolean initialized;
static gint request_pending;


static gboolean drive_info_equal(const RoxDriveInfo *a, const RoxDriveInfo *b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;
    return g_strcmp0(a->name, b->name) == 0 &&
           g_strcmp0(a->device, b->device) == 0 &&
           g_strcmp0(a->label, b->label) == 0 &&
           g_strcmp0(a->fstype, b->fstype) == 0 &&
           g_strcmp0(a->mountpoint, b->mountpoint) == 0 &&
           g_strcmp0(a->size, b->size) == 0 &&
           g_strcmp0(a->type, b->type) == 0 &&
           g_strcmp0(a->transport, b->transport) == 0 &&
           g_strcmp0(a->model, b->model) == 0 &&
           g_strcmp0(a->parent_device, b->parent_device) == 0 &&
           a->removable == b->removable &&
           a->hardware_removable == b->hardware_removable &&
           a->optical == b->optical &&
           a->network == b->network &&
           a->foreign == b->foreign &&
           a->solid_state == b->solid_state &&
           a->label_is_synthetic == b->label_is_synthetic;
}

static gboolean drive_array_equal(GPtrArray *a, GPtrArray *b)
{
    guint i;
    if (a == b)
        return TRUE;
    if (!a || !b || a->len != b->len)
        return FALSE;
    for (i = 0; i < a->len; i++) {
        if (!drive_info_equal(g_ptr_array_index(a, i), g_ptr_array_index(b, i)))
            return FALSE;
    }
    return TRUE;
}

static void subscriber_free(gpointer data)
{
    RoxDrivesSubscriber *sub = data;
    if (!sub)
        return;
    g_weak_ref_clear(&sub->owner);
    g_free(sub);
}

static void notify_target_free(gpointer data)
{
    RoxDrivesNotifyTarget *target = data;
    if (!target)
        return;
    g_clear_object(&target->owner);
    g_free(target);
}

static void compact_subscribers(void)
{
    gint i;
    if (!subscribers)
        return;
    for (i = (gint) subscribers->len - 1; i >= 0; i--) {
        RoxDrivesSubscriber *sub = g_ptr_array_index(subscribers, (guint) i);
        GObject *owner = g_weak_ref_get(&sub->owner);
        if (!owner)
            g_ptr_array_remove_index(subscribers, (guint) i);
        else
            g_object_unref(owner);
    }
}

static void update_poll_state(void);

static void notify_subscribers(GPtrArray *drives, const GError *error)
{
    GPtrArray *targets;
    guint i;

    compact_subscribers();
    if (!subscribers || subscribers->len == 0) {
        update_poll_state();
        return;
    }

    /* Callbacks are allowed to rebuild widgets and thereby subscribe or
     * destroy owners.  Snapshot the live targets first so mutation of the
     * subscriber array cannot invalidate this iteration. */
    targets = g_ptr_array_new_with_free_func(notify_target_free);
    for (i = 0; i < subscribers->len; i++) {
        RoxDrivesSubscriber *sub = g_ptr_array_index(subscribers, i);
        GObject *owner = g_weak_ref_get(&sub->owner);
        RoxDrivesNotifyTarget *target;
        if (!owner)
            continue;
        target = g_new0(RoxDrivesNotifyTarget, 1);
        target->owner = owner;
        target->callback = sub->callback;
        target->user_data = sub->user_data;
        g_ptr_array_add(targets, target);
    }
    for (i = 0; i < targets->len; i++) {
        RoxDrivesNotifyTarget *target = g_ptr_array_index(targets, i);
        target->callback(drives, error, target->user_data);
    }
    g_ptr_array_unref(targets);
    compact_subscribers();
    update_poll_state();
}

static void scan_thread(GTask *task, gpointer source_object,
                        gpointer task_data, GCancellable *cancellable)
{
    GError *error = NULL;
    GPtrArray *drives;
    (void) source_object;
    (void) task_data;
    (void) cancellable;

    drives = rox_drives_read(&error);
    if (!drives) {
        if (error)
            g_task_return_error(task, error);
        else
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                    "%s", _("No usable partitions found"));
        return;
    }
    g_task_return_pointer(task, drives, (GDestroyNotify) g_ptr_array_unref);
}

static void start_scan(void);
static void ensure_initialized(void);

static void scan_done(GObject *source_object, GAsyncResult *result,
                      gpointer user_data)
{
    GError *error = NULL;
    GPtrArray *drives;
    gboolean again;
    (void) source_object;
    (void) user_data;

    drives = g_task_propagate_pointer(G_TASK(result), &error);
    scan_running = FALSE;
    again = scan_again;
    scan_again = FALSE;

    if (drives) {
        gboolean changed = !drive_array_equal(snapshot, drives);
        if (snapshot)
            g_ptr_array_unref(snapshot);
        snapshot = g_ptr_array_ref(drives);
        if (changed)
            notify_subscribers(snapshot, NULL);
        g_ptr_array_unref(drives);
    } else {
        /* A transient scan failure must not destroy the last valid snapshot. */
        notify_subscribers(NULL, error);
        if (error) {
            g_warning("Unable to refresh drives: %s", error->message);
            g_clear_error(&error);
        }
    }

    if (again)
        start_scan();
}

static void start_scan(void)
{
    GTask *task;
    if (scan_running) {
        scan_again = TRUE;
        return;
    }
    scan_running = TRUE;
    task = g_task_new(NULL, NULL, scan_done, NULL);
    g_task_run_in_thread(task, scan_thread);
    g_object_unref(task);
}

static gboolean request_scan_in_main(gpointer data)
{
    (void) data;
    g_atomic_int_set(&request_pending, 0);
    ensure_initialized();
    start_scan();
    return G_SOURCE_REMOVE;
}

void rox_drives_monitor_request_scan(void)
{
    /* This function is called from both GTK callbacks and SMB workers.
     * Always marshal through the default main context and coalesce bursts.
     * g_idle_add() never executes the callback in the worker itself. */
    if (g_atomic_int_compare_and_exchange(&request_pending, 0, 1))
        g_idle_add_full(G_PRIORITY_DEFAULT, request_scan_in_main, NULL, NULL);
}

static void volume_changed(GVolumeMonitor *monitor, gpointer object,
                           gpointer data)
{
    (void) monitor;
    (void) object;
    (void) data;
    compact_subscribers();
    if (subscribers && subscribers->len > 0)
        start_scan();
}

static gboolean poll_cb(gpointer data)
{
    (void) data;
    compact_subscribers();
    if (!subscribers || subscribers->len == 0) {
        poll_source = 0;
        return G_SOURCE_REMOVE;
    }
    start_scan();
    return G_SOURCE_CONTINUE;
}

static void update_poll_state(void)
{
    if (!initialized || !subscribers)
        return;
    compact_subscribers();
    if (subscribers->len > 0) {
        if (!poll_source)
            poll_source = g_timeout_add_seconds(DRIVE_POLL_SECONDS, poll_cb, NULL);
    } else if (poll_source) {
        g_source_remove(poll_source);
        poll_source = 0;
    }
}

static void ensure_initialized(void)
{
    if (initialized)
        return;
    initialized = TRUE;
    subscribers = g_ptr_array_new_with_free_func(subscriber_free);
    volume_monitor = g_volume_monitor_get();
    if (volume_monitor) {
        g_signal_connect(volume_monitor, "mount-added", G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "mount-removed", G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "mount-changed", G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "volume-added", G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "volume-removed", G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "volume-changed", G_CALLBACK(volume_changed), NULL);
    }
}

GPtrArray *rox_drives_monitor_snapshot_copy(void)
{
    GPtrArray *copy;
    guint i;
    ensure_initialized();
    if (!snapshot)
        return NULL;
    copy = g_ptr_array_new_with_free_func(rox_drive_info_free);
    for (i = 0; i < snapshot->len; i++)
        g_ptr_array_add(copy, rox_drive_info_copy(g_ptr_array_index(snapshot, i)));
    return copy;
}

RoxDriveInfo *rox_drives_monitor_find_by_device(const gchar *device)
{
    guint i;
    ensure_initialized();
    if (!device || !snapshot)
        return NULL;
    for (i = 0; i < snapshot->len; i++) {
        RoxDriveInfo *drive = g_ptr_array_index(snapshot, i);
        if (g_strcmp0(drive->device, device) == 0)
            return rox_drive_info_copy(drive);
    }
    return NULL;
}

void rox_drives_monitor_subscribe(GObject *owner,
                                  RoxDrivesMonitorFunc callback,
                                  gpointer user_data)
{
    guint i;
    RoxDrivesSubscriber *sub;
    gboolean first_live;

    g_return_if_fail(G_IS_OBJECT(owner));
    g_return_if_fail(callback != NULL);
    ensure_initialized();
    compact_subscribers();
    first_live = subscribers->len == 0;

    for (i = 0; i < subscribers->len; i++) {
        RoxDrivesSubscriber *existing = g_ptr_array_index(subscribers, i);
        GObject *existing_owner = g_weak_ref_get(&existing->owner);
        gboolean same = existing_owner == owner && existing->callback == callback &&
                        existing->user_data == user_data;
        if (existing_owner)
            g_object_unref(existing_owner);
        if (same) {
            if (snapshot)
                callback(snapshot, NULL, user_data);
            update_poll_state();
            return;
        }
    }

    sub = g_new0(RoxDrivesSubscriber, 1);
    g_weak_ref_init(&sub->owner, owner);
    sub->callback = callback;
    sub->user_data = user_data;
    g_ptr_array_add(subscribers, sub);
    update_poll_state();

    if (snapshot)
        callback(snapshot, NULL, user_data);
    if (!snapshot || first_live)
        start_scan();
}
