project('phosh-op9-quick-settings', 'c', version: '1', license: 'GPL-3.0-or-later')

# Plugins use phosh's own classes, so they need its headers: point
# -Dphosh_src at an unpacked phosh source tree of the installed version.
phosh_src = get_option('phosh_src')
phosh_inc = include_directories(phosh_src / 'src')

plugins_dir = dependency('phosh-plugins').get_variable(pkgconfig: 'quick_setting_plugins_dir')

deps = [
  dependency('phosh-plugins'),
  dependency('gio-2.0'),
  dependency('gtk+-3.0'),
  dependency('libhandy-1'),
]

plugins = {
  'op9-touch-rate-quick-setting': ['OP9_QS_TOUCH_RATE', 'Touch report rate',
                                   'Switch the touchscreen between 360 Hz and 120 Hz'],
  'op9-natural-colour-quick-setting': ['OP9_QS_NATURAL_COLOUR', 'Natural colour',
                                       'Switch between sRGB (Natural) and wide-gamut (Vivid) colours'],
}

foreach name, p : plugins
  shared_module('phosh-plugin-' + name,
    'op9-quick-setting.c',
    c_args: ['-D' + p[0], '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name)],
    include_directories: phosh_inc,
    dependencies: deps,
    install: true,
    install_dir: plugins_dir,
  )

  conf = configuration_data()
  conf.set('id', name)
  conf.set('name', p[1])
  conf.set('comment', p[2])
  conf.set('plugins_dir', plugins_dir)
  configure_file(
    input: 'quick-setting.plugin.in',
    output: name + '.plugin',
    configuration: conf,
    install: true,
    install_dir: plugins_dir,
  )
endforeach
