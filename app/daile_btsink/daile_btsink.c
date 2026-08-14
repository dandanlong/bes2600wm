/****************************************************************************
 * daile_btsink — boot helper: enable BT A2DP Sink (phone → BES)
 *
 * Starts after bluetoothd. Makes the adapter discoverable as "DAILE" so a
 * phone can pair and stream A2DP; mediad graph routes a2dpsnk → pcm0p.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <bluetooth.h>
#include <bt_adapter.h>
#include <bt_device.h>

#ifndef DAILE_BT_NAME
#  define DAILE_BT_NAME "DAILE"
#endif

/* Audio service + Rendering + Audio/Video major + Loudspeaker minor */
#ifndef DAILE_BT_COD
#  define DAILE_BT_COD 0x240414
#endif

static bt_instance_t *g_ins;
static void *g_cb_cookie;
static volatile int g_adapter_on;

static void on_adapter_state_changed(void *cookie, bt_adapter_state_t state)
{
  (void)cookie;
  syslog(LOG_NOTICE, "daile_btsink: adapter state=%d\n", (int)state);
  if (state == BT_ADAPTER_STATE_ON)
    {
      g_adapter_on = 1;
      if (bt_adapter_set_name(g_ins, DAILE_BT_NAME) != BT_STATUS_SUCCESS)
        {
          syslog(LOG_ERR, "daile_btsink: set_name failed\n");
        }
      if (bt_adapter_set_scan_mode(g_ins,
                                   BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE,
                                   true) != BT_STATUS_SUCCESS)
        {
          syslog(LOG_ERR, "daile_btsink: set_scan_mode failed\n");
        }
      if (bt_adapter_set_device_class(g_ins, DAILE_BT_COD) != BT_STATUS_SUCCESS)
        {
          syslog(LOG_ERR, "daile_btsink: set_cod failed\n");
        }
      syslog(LOG_NOTICE,
             "daile_btsink: discoverable name=%s cod=0x%06x\n",
             DAILE_BT_NAME, DAILE_BT_COD);
    }
}

static void on_pair_request(void *cookie, bt_address_t *addr)
{
  (void)cookie;
  (void)bt_device_pair_request_reply(g_ins, addr, true);
  syslog(LOG_NOTICE, "daile_btsink: pair request auto-accepted\n");
}

static void on_pair_display(void *cookie, bt_address_t *addr,
                            bt_transport_t transport, bt_pair_type_t type,
                            uint32_t passkey)
{
  (void)cookie;
  (void)type;
  (void)passkey;
  (void)bt_device_set_pairing_confirmation(g_ins, addr, transport, true);
}

static void on_connect_request(void *cookie, bt_address_t *addr)
{
  (void)cookie;
  (void)bt_device_connect_request_reply(g_ins, addr, true);
  syslog(LOG_NOTICE, "daile_btsink: connect request auto-accepted\n");
}

static const adapter_callbacks_t g_cbs =
{
  .on_adapter_state_changed = on_adapter_state_changed,
  .on_pair_request = on_pair_request,
  .on_pair_display = on_pair_display,
  .on_connect_request = on_connect_request,
};

int daile_btsink_main(int argc, FAR char *argv[])
{
  int i;

  (void)argc;
  (void)argv;

  syslog(LOG_NOTICE, "daile_btsink: start\n");

  for (i = 0; i < 50; i++)
    {
      g_ins = bluetooth_create_instance();
      if (g_ins != NULL)
        {
          break;
        }
      usleep(200000);
    }

  if (g_ins == NULL)
    {
      syslog(LOG_ERR, "daile_btsink: bluetooth_create_instance failed\n");
      return 1;
    }

  g_cb_cookie = bt_adapter_register_callback(g_ins, &g_cbs);
  if (g_cb_cookie == NULL)
    {
      syslog(LOG_ERR, "daile_btsink: register_callback failed\n");
    }

  if (bt_adapter_get_state(g_ins) == BT_ADAPTER_STATE_OFF)
    {
      if (bt_adapter_enable(g_ins) != BT_STATUS_SUCCESS)
        {
          syslog(LOG_ERR, "daile_btsink: enable failed\n");
          return 1;
        }
    }
  else if (bt_adapter_get_state(g_ins) == BT_ADAPTER_STATE_ON)
    {
      on_adapter_state_changed(NULL, BT_ADAPTER_STATE_ON);
    }

  /* Keep process alive for pairing callbacks. */
  for (;;)
    {
      sleep(60);
    }

  return 0;
}
