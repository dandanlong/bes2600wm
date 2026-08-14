#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/rpmsg/rpmsg.h>

#include "hal_usb.h"
#include "usb_descriptor.h"
#include "midi_rpmsg.h"

#define USB_MIDI_EP            2
#define USB_MIDI_PACKET_SIZE   USB_MAX_PACKET_SIZE_BULK
#define MIDI_QUEUE_COUNT       128

struct midi_bridge_s
{
  struct rpmsg_endpoint ept;
  sem_t sem;
  pthread_t worker;
  volatile uint32_t head;
  volatile uint32_t tail;
  struct midi_rpmsg_msg_s queue[MIDI_QUEUE_COUNT];
  bool endpoint_created;
  bool usb_configured;
  uint32_t usb_packets;
  uint32_t dropped;
};

static struct midi_bridge_s g_bridge;
static uint8_t g_usb_rx[USB_MIDI_PACKET_SIZE] __attribute__((aligned(32)));

static const uint8_t g_device_desc[] =
{
  18, DEVICE_DESCRIPTOR,
  0x00, 0x02,             /* USB 2.0 */
  0x00, 0x00, 0x00,
  USB_MAX_PACKET_SIZE_CTRL,
  0xf5, 0x37,             /* VID 0x37f5 (Bestechnic) */
  0x01, 0x08,             /* PID 0x0801 */
  0x00, 0x01,
  1, 2, 3,
  1
};

/* USB MIDI 1.0 AudioControl + MIDIStreaming descriptor, one cable in/out. */
static const uint8_t g_config_desc[] =
{
  9, CONFIGURATION_DESCRIPTOR,
  97, 0,                  /* wTotalLength */
  2, 1, 0, 0x80, 50,

  /* Standard AudioControl interface */
  9, INTERFACE_DESCRIPTOR,
  0, 0, 0, 0x01, 0x01, 0x00, 0,
  /* Class-specific AudioControl header */
  9, 0x24, 0x01,
  0x00, 0x01,             /* bcdADC 1.00 */
  9, 0,                   /* class-specific AC total */
  1, 1,                   /* one streaming interface: #1 */

  /* Standard MIDIStreaming interface */
  9, INTERFACE_DESCRIPTOR,
  1, 0, 2, 0x01, 0x03, 0x00, 4,
  /* Class-specific MIDIStreaming header */
  7, 0x24, 0x01,
  0x00, 0x01,             /* bcdMSC 1.00 */
  47, 0,

  /* Embedded MIDI IN jack, ID 1 */
  6, 0x24, 0x02, 0x01, 1, 0,
  /* External MIDI IN jack, ID 2 */
  6, 0x24, 0x02, 0x02, 2, 0,
  /* Embedded MIDI OUT jack, ID 3, fed by external IN jack */
  9, 0x24, 0x03, 0x01, 3, 1, 2, 1, 0,
  /* External MIDI OUT jack, ID 4, fed by embedded IN jack */
  9, 0x24, 0x03, 0x02, 4, 1, 1, 1, 0,

  /* Host-to-device bulk endpoint */
  7, ENDPOINT_DESCRIPTOR,
  USB_MIDI_EP, E_BULK,
  LSB(USB_MIDI_PACKET_SIZE), MSB(USB_MIDI_PACKET_SIZE), 0,
  5, 0x25, 0x01, 1, 1,

  /* Device-to-host bulk endpoint */
  7, ENDPOINT_DESCRIPTOR,
  0x80 | USB_MIDI_EP, E_BULK,
  LSB(USB_MIDI_PACKET_SIZE), MSB(USB_MIDI_PACKET_SIZE), 0,
  5, 0x25, 0x01, 1, 3
};

static const uint8_t g_lang_desc[] =
{
  4, STRING_DESCRIPTOR, 0x09, 0x04
};
static const uint8_t g_manufacturer_desc[] =
{
  22, STRING_DESCRIPTOR,
  'B', 0, 'e', 0, 's', 0, 't', 0, 'e', 0, 'c', 0, 'h', 0, 'n', 0, 'i', 0,
  'c', 0
};
static const uint8_t g_product_desc[] =
{
  30, STRING_DESCRIPTOR,
  'D', 0, 'A', 0, 'I', 0, 'L', 0, 'E', 0, ' ', 0,
  'U', 0, 'S', 0, 'B', 0, ' ', 0, 'M', 0, 'I', 0, 'D', 0, 'I', 0
};
static const uint8_t g_serial_desc[] =
{
  18, STRING_DESCRIPTOR,
  'D', 0, 'A', 0, 'I', 0, 'L', 0, 'E', 0, '0', 0, '0', 0, '1', 0
};
static const uint8_t g_interface_desc[] =
{
  24, STRING_DESCRIPTOR,
  'M', 0, 'I', 0, 'D', 0, 'I', 0, ' ', 0,
  'B', 0, 'r', 0, 'i', 0, 'd', 0, 'g', 0, 'e', 0
};

static const uint8_t *midi_device_desc(uint8_t type)
{
  return type == DEVICE_DESCRIPTOR ? g_device_desc : NULL;
}

static const uint8_t *midi_config_desc(uint8_t index)
{
  (void)index;
  return g_config_desc;
}

static const uint8_t *midi_string_desc(uint8_t index)
{
  switch (index)
    {
      case 0:
        return g_lang_desc;
      case 1:
        return g_manufacturer_desc;
      case 2:
        return g_product_desc;
      case 3:
        return g_serial_desc;
      case 4:
        return g_interface_desc;
      default:
        return NULL;
    }
}

static bool midi_setup_recv(struct EP0_TRANSFER *transfer)
{
  (void)transfer;
  return false;
}

static bool midi_data_recv(struct EP0_TRANSFER *transfer)
{
  (void)transfer;
  return false;
}

static void midi_queue_event(uint8_t status, uint8_t data1, uint8_t data2)
{
  uint32_t head = g_bridge.head;
  uint32_t next = (head + 1u) & (MIDI_QUEUE_COUNT - 1u);
  struct midi_rpmsg_msg_s *msg;

  if (next == g_bridge.tail)
    {
      g_bridge.dropped++;
      return;
    }

  msg = &g_bridge.queue[head];
  msg->magic = MIDI_RPMSG_MAGIC;
  msg->type = MIDI_RPMSG_EVENT;
  msg->status = status;
  msg->data1 = data1;
  msg->data2 = data2;
  g_bridge.head = next;
  sem_post(&g_bridge.sem);
}

static bool midi_usb_recv_done(const uint8_t *data, uint32_t length,
                               enum XFER_COMPL_STATE state)
{
  uint32_t i;

  if (state == XFER_COMPL_SUCCESS)
    {
      for (i = 0; i + 3u < length; i += 4u)
        {
          uint8_t cin = data[i] & 0x0fu;
          uint8_t status = data[i + 1u];

          if (cin == 0x8u || cin == 0x9u || cin == 0xbu || cin == 0xeu)
            {
              midi_queue_event(status, data[i + 2u], data[i + 3u]);
            }
        }
      g_bridge.usb_packets++;
    }

  if (g_bridge.usb_configured)
    {
      hal_usb_recv_epn(USB_MIDI_EP, g_usb_rx, sizeof(g_usb_rx));
    }
  return true;
}

static bool midi_set_config(uint8_t configuration)
{
  if (configuration != 1)
    {
      g_bridge.usb_configured = false;
      return configuration == 0;
    }

  if (hal_usb_activate_epn(EP_OUT, USB_MIDI_EP, E_BULK,
                           USB_MIDI_PACKET_SIZE) != 0 ||
      hal_usb_activate_epn(EP_IN, USB_MIDI_EP, E_BULK,
                           USB_MIDI_PACKET_SIZE) != 0)
    {
      return false;
    }

  g_bridge.usb_configured = true;
  hal_usb_recv_epn(USB_MIDI_EP, g_usb_rx, sizeof(g_usb_rx));
  syslog(LOG_NOTICE, "usbmidid: USB MIDI configured\n");
  return true;
}

static bool midi_set_interface(uint16_t interface, uint16_t alternate)
{
  return interface < 2 && alternate == 0;
}

static void midi_usb_state(enum HAL_USB_STATE_EVENT event, uint32_t param)
{
  (void)param;
  if (event == HAL_USB_EVENT_RESET || event == HAL_USB_EVENT_DISCONNECT)
    {
      g_bridge.usb_configured = false;
    }
}

static const struct HAL_USB_CALLBACKS g_usb_callbacks =
{
  .device_desc = midi_device_desc,
  .cfg_desc = midi_config_desc,
  .string_desc = midi_string_desc,
  .setuprecv = midi_setup_recv,
  .datarecv = midi_data_recv,
  .setcfg = midi_set_config,
  .setitf = midi_set_interface,
  .state_change = midi_usb_state,
  .epn_recv_compl[USB_MIDI_EP - 1] = midi_usb_recv_done
};

static int midi_rpmsg_callback(struct rpmsg_endpoint *ept, void *data,
                               size_t len, uint32_t src, void *priv)
{
  (void)ept;
  (void)data;
  (void)len;
  (void)src;
  (void)priv;
  return 0;
}

static void midi_rpmsg_unbind(struct rpmsg_endpoint *ept)
{
  (void)ept;
  g_bridge.endpoint_created = false;
}

static bool midi_rpmsg_match(struct rpmsg_device *rdev, void *priv,
                             const char *name, uint32_t dest)
{
  (void)rdev;
  (void)priv;
  (void)dest;
  return strcmp(name, MIDI_RPMSG_ENDPOINT_NAME) == 0;
}

static void midi_rpmsg_bind(struct rpmsg_device *rdev, void *priv,
                            const char *name, uint32_t dest)
{
  int ret;

  (void)priv;
  g_bridge.ept.priv = &g_bridge;
  ret = rpmsg_create_ept(&g_bridge.ept, rdev, name, RPMSG_ADDR_ANY, dest,
                         midi_rpmsg_callback, midi_rpmsg_unbind);
  if (ret == 0)
    {
      g_bridge.endpoint_created = true;
      syslog(LOG_NOTICE, "usbmidid: A7 MIDI endpoint connected\n");
    }
  else
    {
      syslog(LOG_ERR, "usbmidid: RPMSG bind failed %d\n", ret);
    }
}

static void *midi_send_worker(void *arg)
{
  (void)arg;
  for (;;)
    {
      struct midi_rpmsg_msg_s msg;

      while (sem_wait(&g_bridge.sem) < 0 && errno == EINTR)
        {
        }

      while (g_bridge.tail != g_bridge.head)
        {
          msg = g_bridge.queue[g_bridge.tail];
          g_bridge.tail = (g_bridge.tail + 1u) & (MIDI_QUEUE_COUNT - 1u);

          if (g_bridge.endpoint_created &&
              is_rpmsg_ept_ready(&g_bridge.ept))
            {
              rpmsg_send(&g_bridge.ept, &msg, sizeof(msg));
            }
          else
            {
              g_bridge.dropped++;
            }
        }
    }
  return NULL;
}

int usbmidid_main(int argc, char *argv[])
{
  pthread_attr_t attr;
  struct sched_param param;
  int ret;

  (void)argc;
  (void)argv;
  memset(&g_bridge, 0, sizeof(g_bridge));
  sem_init(&g_bridge.sem, 0, 0);

  ret = rpmsg_register_callback(&g_bridge, NULL, NULL,
                                midi_rpmsg_match, midi_rpmsg_bind);
  if (ret != 0)
    {
      syslog(LOG_ERR, "usbmidid: RPMSG register failed %d\n", ret);
      return 1;
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 4096);
  param.sched_priority = 110;
  pthread_attr_setschedparam(&attr, &param);
  ret = pthread_create(&g_bridge.worker, &attr, midi_send_worker, NULL);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      syslog(LOG_ERR, "usbmidid: worker create failed %d\n", ret);
      return 2;
    }

  ret = hal_usb_open(&g_usb_callbacks, HAL_USB_API_NONBLOCKING);
  if (ret != 0)
    {
      syslog(LOG_ERR, "usbmidid: hal_usb_open failed %d\n", ret);
      return 3;
    }

  syslog(LOG_NOTICE,
         "usbmidid: DAILE USB MIDI ready (VID=37f5 PID=0801)\n");
  for (;;)
    {
      sleep(10);
      syslog(LOG_INFO,
             "usbmidid: configured=%d packets=%lu dropped=%lu rpmsg=%d\n",
             g_bridge.usb_configured,
             (unsigned long)g_bridge.usb_packets,
             (unsigned long)g_bridge.dropped,
             g_bridge.endpoint_created);
    }
  return 0;
}
