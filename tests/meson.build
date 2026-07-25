config_test = executable(
  'test-config',
  'test-config.c',
  wl_proto_headers,
  link_with: [common, tokenize],
  dependencies: [pixman, xkb, fontconfig, wayland_client, fcft, tllist])

test('config', config_test)

tab_close_test = executable(
  'test-tab-close',
  'test-tab-close.c',
  '../tab-close.c',
  wl_proto_headers,
  dependencies: [pixman, xkb, fontconfig, wayland_client, fcft, tllist])

test('tab-close', tab_close_test)

tab_activity_test = executable(
  'test-tab-activity',
  'test-tab-activity.c',
  '../tab-activity.c')

test('tab-activity', tab_activity_test)
