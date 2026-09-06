project(
  'rox-filer2',
  'c',
  version : '2.12.2-89',
  meson_version : '>=0.61.0',
  default_options : [
    'c_std=gnu11',
    'warning_level=1',
    'buildtype=release',
  ],
)

cc = meson.get_compiler('c')

# Core dependencies. Keep these aligned with the historical autoconf build.
gtk_dep = dependency('gtk+-3.0', version : '>=3.22.0')
gio_unix_dep = dependency('gio-unix-2.0', version : '>=2.70.0')
libxml_dep = dependency('libxml-2.0', version : '>=2.0.0')
sm_dep = dependency('sm', version : '>=1.0')
ice_dep = dependency('ice')
x11_dep = dependency('x11')
shared_mime_info_dep = dependency('shared-mime-info', version : '>=0.14')
pango_dep = dependency('pango', version : '>=1.1.2', required : false)
smbclient_dep = dependency('smbclient', required : get_option('smb'))

m_dep = cc.find_library('m', required : false)
dl_dep = cc.find_library('dl', required : false)
intl_dep = cc.find_library('intl', required : false)

conf = configuration_data()
conf.set_quoted('PROJECT', 'ROX-Filer')
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('GTK_VERSION', gtk_dep.version())
conf.set('ROX_USING_GTK3', true)
conf.set('ROX_GTK3', true)
conf.set('STDC_HEADERS', true)

header_checks = {
  'HAVE_SYS_UCRED_H' : 'sys/ucred.h',
  'HAVE_MNTENT_H' : 'mntent.h',
  'HAVE_SYS_MNTENT_H' : 'sys/mntent.h',
  'HAVE_FCNTL_H' : 'fcntl.h',
  'HAVE_APSYMBOLS_H' : 'apsymbols.h',
  'HAVE_APBUILD_APSYMBOLS_H' : 'apbuild/apsymbols.h',
  'HAVE_SYS_STATVFS_H' : 'sys/statvfs.h',
  'HAVE_SYS_VFS_H' : 'sys/vfs.h',
  'HAVE_WCTYPE_H' : 'wctype.h',
  'HAVE_LIBINTL_H' : 'libintl.h',
  'HAVE_SYS_INOTIFY_H' : 'sys/inotify.h',
  'HAVE_SYS_MMAN_H' : 'sys/mman.h',
  'HAVE_ATTR_XATTR_H' : 'attr/xattr.h',
  'HAVE_SYS_XATTR_H' : 'sys/xattr.h',
}
foreach macro, header : header_checks
  conf.set(macro, cc.has_header(header))
endforeach

conf.set('HAVE_GETOPT_LONG', cc.has_function(
  'getopt_long',
  prefix : '#include <getopt.h>',
  dependencies : [],
))
conf.set('HAVE_UNSETENV', cc.has_function('unsetenv', prefix : '#include <stdlib.h>'))
conf.set('HAVE_STATVFS', cc.has_function('statvfs', prefix : '#include <sys/statvfs.h>'))
conf.set('HAVE_STATFS', cc.has_function('statfs', prefix : '#include <sys/vfs.h>'))
conf.set('HAVE_MBRTOWC', cc.has_function('mbrtowc', prefix : '#include <wchar.h>'))
conf.set('HAVE_GETXATTR', cc.has_function(
  'getxattr',
  prefix : '#include <sys/types.h>\n#include <sys/xattr.h>',
))
conf.set('HAVE_ATTROPEN', cc.has_function('attropen'))
conf.set('HAVE_MMAP', cc.has_function('mmap', prefix : '#include <sys/mman.h>'))

file_prog = find_program('file', required : false)
file_b_flag = false
if file_prog.found()
  file_b_flag = run_command(file_prog, '-b', '/', check : false).returncode() == 0
endif
conf.set('FILE_B_FLAG', file_b_flag)

# Modern Linux targets use 64-bit off_t by default. This mirrors the intent of
# the historical getconf LFS64_CFLAGS test without baking distro-specific flags
# into the build system.
off_t_size = cc.sizeof('off_t', prefix : '#include <sys/types.h>')
conf.set('LARGE_FILE_SUPPORT', off_t_size >= 8)
conf.set('USE_PANGO_WRAP_WORD_CHAR', pango_dep.found())
conf.set('HAVE_LIBSMBCLIENT', smbclient_dep.found())

configure_file(
  input : 'ROX-Filer/src/config.h.meson.in',
  output : 'config.h',
  configuration : conf,
)

rox_sources = files(
  'ROX-Filer/src/abox.c',
  'ROX-Filer/src/action.c',
  'ROX-Filer/src/appinfo.c',
  'ROX-Filer/src/appmenu.c',
  'ROX-Filer/src/bind.c',
  'ROX-Filer/src/bookmarks.c',
  'ROX-Filer/src/bulk_rename.c',
  'ROX-Filer/src/cell_icon.c',
  'ROX-Filer/src/choices.c',
  'ROX-Filer/src/collection.c',
  'ROX-Filer/src/dir.c',
  'ROX-Filer/src/diritem.c',
  'ROX-Filer/src/desktop.c',
  'ROX-Filer/src/desktop-backend.c',
  'ROX-Filer/src/desktop-x11.c',
  'ROX-Filer/src/desktop-wayland.c',
  'ROX-Filer/src/desktop_apps.c',
  'ROX-Filer/src/desktop_dropdown.c',
  'ROX-Filer/src/desktop_wbar.c',
  'ROX-Filer/src/display.c',
  'ROX-Filer/src/dnd.c',
  'ROX-Filer/src/drives.c',
  'ROX-Filer/src/drives_monitor.c',
  'ROX-Filer/src/dropbox.c',
  'ROX-Filer/src/filer.c',
  'ROX-Filer/src/filer_pair.c',
  'ROX-Filer/src/find.c',
  'ROX-Filer/src/fscache.c',
  'ROX-Filer/src/custom_actions.c',
  'ROX-Filer/src/gtksavebox.c',
  'ROX-Filer/src/rox_itemfactory.c',
  'ROX-Filer/src/gui_support.c',
  'ROX-Filer/src/i18n.c',
  'ROX-Filer/src/image_mounter.c',
  'ROX-Filer/src/icon.c',
  'ROX-Filer/src/infobox.c',
  'ROX-Filer/src/interface_style.c',
  'ROX-Filer/src/modern_ui.c',
  'ROX-Filer/src/log.c',
  'ROX-Filer/src/debug_log.c',
  'ROX-Filer/src/main.c',
  'ROX-Filer/src/menu.c',
  'ROX-Filer/src/minibuffer.c',
  'ROX-Filer/src/modechange.c',
  'ROX-Filer/src/mount.c',
  'ROX-Filer/src/options.c',
  'ROX-Filer/src/panel.c',
  'ROX-Filer/src/pixmaps.c',
  'ROX-Filer/src/rox_config.c',
  'ROX-Filer/src/remote.c',
  'ROX-Filer/src/run.c',
  'ROX-Filer/src/sc.c',
  'ROX-Filer/src/session.c',
  'ROX-Filer/src/support.c',
  'ROX-Filer/src/search_integration.c',
  'ROX-Filer/src/smb.c',
  'ROX-Filer/src/toolbar.c',
  'ROX-Filer/src/trash.c',
  'ROX-Filer/src/type.c',
  'ROX-Filer/src/usericons.c',
  'ROX-Filer/src/xdg_apps.c',
  'ROX-Filer/src/view_collection.c',
  'ROX-Filer/src/view_details.c',
  'ROX-Filer/src/view_iface.c',
  'ROX-Filer/src/wrapped.c',
  'ROX-Filer/src/xml.c',
  'ROX-Filer/src/xtypes.c',
  'ROX-Filer/src/xdgmime.c',
  'ROX-Filer/src/xdgmimeglob.c',
  'ROX-Filer/src/xdgmimeint.c',
  'ROX-Filer/src/xdgmimemagic.c',
  'ROX-Filer/src/xdgmimeparent.c',
  'ROX-Filer/src/xdgmimealias.c',
  'ROX-Filer/src/xdgmimecache.c',
)

rox_deps = [
  gtk_dep,
  gio_unix_dep,
  libxml_dep,
  sm_dep,
  ice_dep,
  x11_dep,
]
if m_dep.found()
  rox_deps += [m_dep]
endif
if dl_dep.found()
  rox_deps += [dl_dep]
endif
if intl_dep.found()
  rox_deps += [intl_dep]
endif
if smbclient_dep.found()
  rox_deps += [smbclient_dep]
endif

rox_c_args = cc.get_supported_arguments(
  '-Wall',
  '-Wmissing-prototypes',
  '-Wno-pointer-sign',
  '-DHAVE_CONFIG_H=1',
)

rox_filer = executable(
  'ROX-Filer',
  rox_sources,
  include_directories : include_directories('.', 'ROX-Filer/src'),
  dependencies : rox_deps,
  c_args : rox_c_args,
  install : false,
)

rox_find_c_args = cc.get_supported_arguments('-Wall', '-Wextra', '-Wformat=2') + [
  '-DGETTEXT_PACKAGE="rox-find"',
  '-DLOCALEDIR="/usr/share/locale"',
]

rox_find = executable(
  'rox-find',
  'rox-find/rox-find.c',
  dependencies : [gtk_dep, dependency('gio-2.0')],
  c_args : rox_find_c_args,
  install : false,
)

summary(
  {
    'ROX-Filer binary' : join_paths(meson.project_build_root(), 'ROX-Filer'),
    'rox-find binary' : join_paths(meson.project_build_root(), 'rox-find'),
    'GTK3' : gtk_dep.version(),
    'Pango word wrapping' : pango_dep.found(),
    '64-bit file offsets' : off_t_size >= 8,
    'SMB discovery (libsmbclient)' : smbclient_dep.found(),
  },
  section : 'Rox-Filer2 build',
  bool_yn : true,
)
