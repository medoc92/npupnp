project(
  'libnpupnp',
  'cpp',
  license: 'BSD-3-Clause',
  version: '6.1.2',
  default_options: 'cpp_std=c++17',
  meson_version: '>=0.49',
)

# Change this only when the library interface would become incompatible with a binary linked with a
# previous version. A change should also result in changing the Debian binary package name so that
# both library versions can coexist on a system.
npupnp_soversion = '13'
# This is just to change the lib file name when the version changes without changing the API, and
# I'm not quite sure that it is really necessary
npupnp_soversion_minor = '.2.0'

cpp = meson.get_compiler('cpp')
deps = []
if host_machine.system() == 'windows'
  deps += cpp.find_library('ws2_32')
  deps += cpp.find_library('iphlpapi')
elif host_machine.system() == 'sunos'
  deps += cpp.find_library('nsl')
  deps += cpp.find_library('socket')
endif

deps += dependency('threads')
deps += dependency('libcurl')
deps += dependency('libmicrohttpd')
expat_dep = dependency('expat', required: get_option('expat'))
deps += expat_dep

if get_option('default_library') != 'static'
  add_project_arguments('-DDLL_EXPORT', language: 'cpp')
else
  add_project_arguments('-DUPNP_STATIC_LIB', language: 'cpp')
endif

# Note: meson sets -D_FILE_OFFSET_BITS=64 by default
add_project_arguments('-DNOMINMAX', language: 'cpp')
add_project_arguments('-DSMALLUT_NO_REGEX', language: 'cpp')
add_project_arguments('-DSMALLUT_EXTERNAL_INSTANTIATIONS', language: 'cpp')

add_project_arguments(
  cpp.get_supported_arguments(
    '-Wno-deprecated-declarations',
    '/D_CRT_SECURE_NO_WARNINGS',
    '/wd4251',
  ),
  language: 'cpp',
)

libnpupnp_incdir = include_directories('.', 'inc', 'src/inc')

npupnp_sources = files(
  'src/api/upnpapi.cpp',
  'src/api/upnpdebug.cpp',
  'src/dispatcher/miniserver.cpp',
  'src/threadutil/ThreadPool.cpp',
  'src/threadutil/TimerThread.cpp',
  'src/utils/description.cpp',
  'src/utils/genut.cpp',
  'src/utils/httputils.cpp',
  'src/utils/md5.cpp',
  'src/utils/netif.cpp',
  'src/utils/smallut.cpp',
  'src/utils/statcodes.cpp',
  'src/utils/uri.cpp',
  'src/utils/utf8iter.cpp',
  'src/webserver/webserver.cpp',
)

if get_option('gena')
  npupnp_sources += files(
    'src/gena/gena_callback2.cpp',
    'src/gena/gena_ctrlpt.cpp',
    'src/gena/gena_device.cpp',
    'src/gena/gena_sids.cpp',
    'src/gena/service_table.cpp',
  )
endif

if get_option('soap')
  npupnp_sources += files(
    'src/soap/soap_ctrlpt.cpp',
    'src/soap/soap_device.cpp',
  )
endif

if get_option('ssdp')
  npupnp_sources += files(
    'src/ssdp/ssdp_ctrlpt.cpp',
    'src/ssdp/ssdp_device.cpp',
    'src/ssdp/ssdp_server.cpp',
    'src/ssdp/ssdpparser.cpp',
  )
endif

if get_option('tools')
  npupnp_sources += files(
    'src/api/upnptools.cpp',
  )
endif

# The library has 3 configuration files :
# 1) "./autoconfig.h" is auto-generated and used only internally during build
# 2) "./src/inc/config.h" is static and contains some compile-time 
#    parameters. This file was previously in "./upnp/inc" but is no longer 
#    installed (contains internal definitions only).
# 3) "./inc/upnpconfig.h" is auto-generated and installed with the 
#    libraries : it contains information on the configuration of the
#    installed libraries.

auto = configuration_data()
auto.set_quoted('NPUPNP_VERSION_STRING', meson.project_version())
ver = meson.project_version().split('.')
auto.set('NPUPNP_VERSION_MAJOR', ver[0])
auto.set('NPUPNP_VERSION_MINOR', ver[1])
auto.set('NPUPNP_VERSION_PATCH', ver[2])
auto.set10('UPNP_HAVE_DEBUG', get_option('debug'))
auto.set10('UPNP_HAVE_DEVICE', get_option('device'))
auto.set('USE_EXPAT', expat_dep.found())
auto.set10('UPNP_HAVE_GENA', get_option('gena'))
auto.set('UPNP_HAVE_OPTSSDP', get_option('optssdp'))
auto.set10('UPNP_HAVE_SOAP', get_option('soap'))
auto.set10('UPNP_HAVE_SSDP', get_option('ssdp'))
auto.set10('UPNP_HAVE_TOOLS', get_option('tools') or get_option('webserver'))
auto.set('UPNP_ENABLE_UNSPECIFIED_SERVER', get_option('unspecified_server'))
auto.set('UPNP_ENABLE_IPV6', get_option('ipv6'))
auto.set10('UPNP_HAVE_CLIENT', get_option('client'))
auto.set10('UPNP_HAVE_WEBSERVER', get_option('webserver'))

ufile = configure_file(output: 'upnpconfig.h', configuration: auto)
cfile = configure_file(output: 'autoconfig.h', configuration: auto)

libnpupnp = library(
  'libnpupnp',
  npupnp_sources,
  gnu_symbol_visibility: 'hidden',
  name_prefix: '',
  version: npupnp_soversion + npupnp_soversion_minor,
  soversion: host_machine.system() == 'windows' ? '' : npupnp_soversion,
  include_directories: libnpupnp_incdir,
  dependencies: deps,
  install: true,
)

pkg = import('pkgconfig')
pkg.generate(
  libnpupnp,
  description: 'Another SDK for UPnP Devices',
  subdirs: 'npupnp',
)

install_headers(
  'inc/netif.h',
  'inc/upnpdebug.h',
  'inc/upnpdescription.h',
  'inc/UpnpGlobal.h',
  'inc/upnp.h',
  'inc/upnptools.h',
  ufile,
  subdir: 'npupnp',
)

libnpupnp_dep = declare_dependency(
  compile_args: get_option('default_library') == 'static' ? '-DUPNP_STATIC_LIB' : [],
  include_directories: libnpupnp_incdir,
  link_with: libnpupnp,
)

if get_option('testmains')
    subdir('test')
endif
