config_test = executable(
  'test-config',
  'test-config.c',
  '../tab-activity.c',
  wl_proto_headers,
  link_with: [common, tokenize],
  dependencies: [pixman, xkb, fontconfig, wayland_client, fcft, tllist])

test('config', config_test)

help_layout_test = executable(
  'test-help-layout',
  'test-help-layout.c',
  '../help-layout.c')

test('help-layout', help_layout_test)

tab_close_test = executable(
  'test-tab-close',
  'test-tab-close.c',
  '../tab-close.c')

test('tab-close', tab_close_test)

tab_pin_test = executable(
  'test-tab-pin',
  'test-tab-pin.c',
  '../tab-pin.c')

test('tab-pin', tab_pin_test)

tab_activity_test = executable(
  'test-tab-activity',
  'test-tab-activity.c',
  '../tab-activity.c')

test('tab-activity', tab_activity_test)

window_state_test = executable(
  'test-window-state',
  'test-window-state.c',
  '../window-state.c',
  dependencies: [math])

test('window-state', window_state_test)
