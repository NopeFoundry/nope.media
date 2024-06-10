project(
  'nope.media',
  'c',
  default_options: ['c_std=c17'],
  license: 'LGPL-2.1',
  meson_version: '>= 0.57.0',
  version: files('VERSION'),
)

conf_data = configuration_data()
version_array = meson.project_version().split('.')
conf_data.set('version_major', version_array[0])
conf_data.set('version_minor', version_array[1])
conf_data.set('version_micro', version_array[2])

host_system = host_machine.system()
cc = meson.get_compiler('c')

install_rpath = get_option('rpath') ? get_option('prefix') / get_option('libdir') : ''

# This trim prefix is used to make __FILE__ starts from the source dir with
# out-of-tree builds.
# Adjusted from https://github.com/mesonbuild/meson/issues/7485
trim_prefix = run_command([
  find_program('python'), '-c', 'import sys,os;print(os.path.relpath(*sys.argv[1:3]))',
  meson.current_source_dir(),
  meson.project_build_root(),
], check: true).stdout().strip()

add_project_arguments(
  cc.get_supported_arguments([
    '-Werror=missing-prototypes',
    '-fmacro-prefix-map=@0@/='.format(trim_prefix),
  ]),
  language: 'c',
)

if get_option('trace')
  add_project_arguments('-DENABLE_DBG=1', language: 'c')
endif


#
# Dependencies, sometimes shared with the tools and tests
#

lib_deps = [
  cc.find_library('m', required: false),
  dependency('threads'),
  dependency('libavformat', version: '>= 60.16.100'),
  dependency('libavfilter', version: '>= 9.12.100'),
  dependency('libavcodec', version: '>= 60.31.102'),
  dependency('libavutil', version: '>= 58.29.100'),
]

vaapi_dep = dependency('libva', version: '>= 1.1.0', required: get_option('vaapi'))
if vaapi_dep.found()
  lib_deps += vaapi_dep
  add_project_arguments('-DHAVE_VAAPI_HWACCEL=1', language: 'c')
endif

if host_system == 'darwin'
  lib_deps += dependency('appleframeworks', modules: [
    'CoreFoundation',
    'CoreMedia',
    'CoreVideo',
    'QuartzCore',
    'VideoToolbox',
  ])
endif


#
# Library
#

lib_src = files(
  'src/api.c',
  'src/async.c',
  'src/decoder_ffmpeg.c',
  'src/decoders.c',
  'src/log.c',
  'src/mod_decoding.c',
  'src/mod_demuxing.c',
  'src/mod_filtering.c',
  'src/msg.c',
  'src/utils.c',
)

lib_c_args = []
if get_option('default_library') == 'shared'
  lib_c_args += '-DBUILD_NOPE_MEDIA_SHARED_LIB'
else
  lib_c_args += '-DUSE_NOPE_MEDIA_STATIC_LIB'
endif

if host_system == 'darwin'
  lib_src += files('src/decoder_vt.c')
endif

libnopemd = library(
  'nopemd',
  lib_src,
  dependencies: lib_deps,
  install: true,
  install_rpath: install_rpath,
  version: meson.project_version(),
  c_args: lib_c_args,
  gnu_symbol_visibility: 'hidden',
)

lib_header = configure_file(
  input: files('src/nopemd.h.in'),
  output: 'nopemd.h',
  configuration: conf_data
)
install_headers(lib_header)
if get_option('cpp-header')
  install_headers(files('src/nopemd.hpp'))
endif

pkg = import('pkgconfig')
pkg_extra_cflags = []
if get_option('default_library') == 'static'
  pkg_extra_cflags += '-DUSE_NOPE_MEDIA_STATIC_LIB'
endif

pkg.generate(
  libnopemd,
  name: 'libnopemd',  # not specifying the name would fallback on "nopemd.pc" instead of "libnopemd.pc"
  description: 'nope.media library',
  extra_cflags: pkg_extra_cflags,
)


#
# Player
#

player_deps = lib_deps + [
  dependency('sdl2', required: get_option('player')),
]
player_deps_found = true
foreach dep : player_deps
  player_deps_found = player_deps_found and dep.found()
endforeach
if player_deps_found
  player = executable(
    'nope-media',
    files('src/nope-media.c'),
    dependencies: player_deps,
    link_with: libnopemd,
    install: true,
    install_rpath: install_rpath,
  )
endif


#
# Tests
#

if get_option('tests')
  media = files('tests/media.mkv')
  image = files('tests/image.jpg')

  exe_names = [
    'audio',
    'audio_seek',
    'audio_start_end_time',
    'comb',
    'high_refresh_rate',
    'image',
    'image_seek',
    'misc_events',
    'microseconds',
    'next_frame',
    'notavail_file',
    'seek_after_eos',
  ]

  executables = {}
  foreach exe_name : exe_names
    exe = executable(
      'test_' + exe_name,
      files('tests/test_@0@.c'.format(exe_name)),
      dependencies: lib_deps,
      link_with: libnopemd,
      install: false,
      # pkg-config extra cflags don't apply to local executables, so we have to
      # transfer them manually. Alternatively, we could create a virtual
      # dependency and use it.
      # See https://github.com/mesonbuild/meson/issues/8082 for more information
      c_args: pkg_extra_cflags,
    )
    executables += {exe_name: exe}
  endforeach

  tests = {
    'Audio seek':                                  {'test': 'audio_seek',        'args': [media]},
    'Audio':                                       {'test': 'audio',             'args': [media]},
    'Audio start/end time':                        {'test': 'audio_start_end_time', 'args': [media]},
    'Combination audio':                           {'test': 'comb',              'args': [media, 0b100.to_string()]},
    'Combination audio+end':                       {'test': 'comb',              'args': [media, 0b110.to_string()]},
    'Combination audio+end+start':                 {'test': 'comb',              'args': [media, 0b111.to_string()]},
    'Combination audio+start':                     {'test': 'comb',              'args': [media, 0b101.to_string()]},
    'Combination video':                           {'test': 'comb',              'args': [media, 0b000.to_string()]},
    'Combination video+end':                       {'test': 'comb',              'args': [media, 0b010.to_string()]},
    'Combination video+end+start':                 {'test': 'comb',              'args': [media, 0b011.to_string()]},
    'Combination video+start':                     {'test': 'comb',              'args': [media, 0b001.to_string()]},
    'File not available':                          {'test': 'notavail_file'},
    'High refresh rate':                           {'test': 'high_refresh_rate', 'args': [media]},
    'Image Seek':                                  {'test': 'image_seek',        'args': [image]},
    'Image':                                       {'test': 'image',             'args': [image]},
    'Microseconds':                                {'test': 'microseconds',      'args': [media]},
    'Misc events image':                           {'test': 'misc_events',       'args': [image]},
    'Misc events media':                           {'test': 'misc_events',       'args': [media]},
    'Next frame':                                  {'test': 'next_frame',        'args': [media]},
    'Seek after EOS audio':                        {'test': 'seek_after_eos',    'args': [media, 0b000.to_string()]},
    'Seek after EOS audio+end':                    {'test': 'seek_after_eos',    'args': [media, 0b010.to_string()]},
    'Seek after EOS audio+end+start':              {'test': 'seek_after_eos',    'args': [media, 0b001.to_string()]},
    'Seek after EOS audio+start':                  {'test': 'seek_after_eos',    'args': [media, 0b011.to_string()]},
    'Seek after EOS video':                        {'test': 'seek_after_eos',    'args': [media, 0b100.to_string()]},
    'Seek after EOS video+end':                    {'test': 'seek_after_eos',    'args': [media, 0b110.to_string()]},
    'Seek after EOS video+end+start':              {'test': 'seek_after_eos',    'args': [media, 0b101.to_string()]},
    'Seek after EOS video+start':                  {'test': 'seek_after_eos',    'args': [media, 0b111.to_string()]},
  }

  foreach use_pkt_duration : [0, 1]
    foreach test_name, test_data : tests
      test_exe = executables.get(test_data.get('test'))
      test_args = test_data.get('args', [])
      if use_pkt_duration == 1
        test_name += ' with packet duration'
        test_args += '1'
      endif
      test(test_name, test_exe, args: test_args, timeout: 60*60)
    endforeach
  endforeach
endif
