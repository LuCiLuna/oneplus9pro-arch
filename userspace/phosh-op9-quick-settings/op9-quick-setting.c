/*
 * OnePlus 9 Pro quick settings for phosh: a toggle that writes a sysfs knob,
 * remembers the choice and applies it again when phosh starts.
 *
 * Built twice, with OP9_QS_TOUCH_RATE or OP9_QS_NATURAL_COLOUR:
 *   touch-rate      the touchscreen's report rate, 360 Hz or stock's 120 Hz
 *   natural-colour  sRGB colours (stock's "Natural") or the panel's native
 *                   wide gamut (stock's default "Vivid")
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "quick-setting.h"
#include "phosh-plugin.h"

#include <errno.h>
#include <stdio.h>

/* g_io_<module>_query, after the library name libphosh-plugin-<QS_ID>.so */
#if defined(OP9_QS_TOUCH_RATE)
# define QS_QUERY	g_io_phosh_plugin_op9_touch_rate_quick_setting_query
# define QS_ID		"op9-touch-rate-quick-setting"
# define QS_KEY		"touch-360hz"
# define QS_GLOB_DIR	"/sys/bus/i2c/drivers/synaptics-tcm-i2c"
# define QS_GLOB_FILE	"report_rate"
# define QS_ON_VALUE	"360"
# define QS_OFF_VALUE	"120"
# define QS_ON_INFO	"Touch 360 Hz"
# define QS_OFF_INFO	"Touch 120 Hz"
# define QS_ICON	"input-touchpad-symbolic"
# define QsType		PhoshOp9TouchRateQuickSetting
# define qs_type	phosh_op9_touch_rate_quick_setting
#elif defined(OP9_QS_NATURAL_COLOUR)
# define QS_QUERY	g_io_phosh_plugin_op9_natural_colour_quick_setting_query
# define QS_ID		"op9-natural-colour-quick-setting"
# define QS_KEY		"natural-colour"
# define QS_PATH	"/sys/module/msm/parameters/natural_colour"
# define QS_ON_VALUE	"Y"
# define QS_OFF_VALUE	"N"
# define QS_ON_INFO	"Natural colour"
# define QS_OFF_INFO	"Vivid colour"
# define QS_ICON	"applications-graphics-symbolic"
# define QsType		PhoshOp9NaturalColourQuickSetting
# define qs_type	phosh_op9_natural_colour_quick_setting
#else
# error "build with OP9_QS_TOUCH_RATE or OP9_QS_NATURAL_COLOUR"
#endif

#define QS_PASTE_(a, b)	a##b
#define QS_PASTE(a, b)	QS_PASTE_(a, b)
#define QS_FN(name)	QS_PASTE(qs_type, _##name)

typedef struct {
  PhoshQuickSetting parent;
  PhoshStatusIcon  *info;
  char             *path;
} QsType;

typedef struct {
  PhoshQuickSettingClass parent_class;
} QS_PASTE(QsType, Class);

G_DEFINE_TYPE (QsType, qs_type, PHOSH_TYPE_QUICK_SETTING)

char **QS_QUERY (void);


static char *
find_knob (void)
{
#ifdef QS_PATH
  return g_strdup (QS_PATH);
#else
  g_autoptr (GDir) dir = g_dir_open (QS_GLOB_DIR, 0, NULL);
  const char *name;

  while (dir && (name = g_dir_read_name (dir))) {
    g_autofree char *path = g_build_filename (QS_GLOB_DIR, name, QS_GLOB_FILE, NULL);

    if (g_file_test (path, G_FILE_TEST_EXISTS))
      return g_steal_pointer (&path);
  }
  return NULL;
#endif
}


static char *
config_path (void)
{
  return g_build_filename (g_get_user_config_dir (), "op9-tuning.ini", NULL);
}


/* The saved choice: TRUE/FALSE, or -1 when there is none */
static int
load_choice (void)
{
  g_autoptr (GKeyFile) kf = g_key_file_new ();
  g_autofree char *path = config_path ();
  g_autoptr (GError) err = NULL;
  gboolean on;

  if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL))
    return -1;

  on = g_key_file_get_boolean (kf, "quick-settings", QS_KEY, &err);
  return err ? -1 : on;
}


static void
save_choice (gboolean on)
{
  g_autoptr (GKeyFile) kf = g_key_file_new ();
  g_autofree char *path = config_path ();
  g_autoptr (GError) err = NULL;

  g_key_file_load_from_file (kf, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
  g_key_file_set_boolean (kf, "quick-settings", QS_KEY, on);
  if (!g_key_file_save_to_file (kf, path, &err))
    g_warning ("Failed to save %s: %s", path, err->message);
}


static gboolean
read_knob (QsType *self)
{
  g_autofree char *contents = NULL;

  if (!self->path || !g_file_get_contents (self->path, &contents, NULL, NULL))
    return FALSE;

  return g_str_has_prefix (g_strstrip (contents), QS_ON_VALUE);
}


static gboolean
write_knob (QsType *self, gboolean on)
{
  g_autoptr (GError) err = NULL;
  const char *value = on ? QS_ON_VALUE : QS_OFF_VALUE;
  FILE *f;

  if (!self->path)
    return FALSE;

  /* sysfs wants one write(); g_file_set_contents() would replace the file */
  f = fopen (self->path, "w");
  if (!f || fputs (value, f) < 0 || fclose (f) != 0) {
    g_warning ("Failed to write %s to %s: %s", value, self->path, g_strerror (errno));
    return FALSE;
  }
  return TRUE;
}


static void
sync_ui (QsType *self)
{
  gboolean on = read_knob (self);

  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), on);
  phosh_status_icon_set_info (self->info, self->path ? (on ? QS_ON_INFO : QS_OFF_INFO) :
                              "Not available");
}


static void
on_clicked (QsType *self)
{
  gboolean on = !read_knob (self);

  if (write_knob (self, on))
    save_choice (on);
  sync_ui (self);
}


static void
QS_FN (finalize) (GObject *object)
{
  QsType *self = (QsType *)object;

  g_free (self->path);
  G_OBJECT_CLASS (QS_FN (parent_class))->finalize (object);
}


static void
QS_FN (class_init) (QS_PASTE (QsType, Class) *klass)
{
  G_OBJECT_CLASS (klass)->finalize = QS_FN (finalize);
}


static void
QS_FN (init) (QsType *self)
{
  int saved;

  self->info = PHOSH_STATUS_ICON (phosh_status_icon_new ());
  phosh_status_icon_set_icon_name (self->info, QS_ICON);
  phosh_status_icon_set_pixel_size (self->info, 16);
  gtk_widget_set_visible (GTK_WIDGET (self->info), TRUE);
  /* phosh doesn't export the setter; the property is what its .ui files use */
  g_object_set (self, "status-icon", self->info, NULL);

  g_signal_connect_swapped (self, "clicked", G_CALLBACK (on_clicked), self);

  self->path = find_knob ();

  /* Apply the remembered choice: sysfs settings don't survive a reboot */
  saved = load_choice ();
  if (saved >= 0 && saved != read_knob (self))
    write_knob (self, saved);

  sync_ui (self);
}


void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));

  g_io_extension_point_implement (PHOSH_PLUGIN_EXTENSION_POINT_QUICK_SETTING_WIDGET,
                                  QS_FN (get_type) (),
                                  QS_ID,
                                  10);
}


void
g_io_module_unload (GIOModule *module)
{
}


char **
QS_QUERY (void)
{
  char *extension_points[] = { PHOSH_PLUGIN_EXTENSION_POINT_QUICK_SETTING_WIDGET, NULL };

  return g_strdupv (extension_points);
}
