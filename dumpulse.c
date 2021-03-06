/* Implementation of Dumpulse, a dumb heartbeat daemon in 260 bytes of
   RAM and ≈350 bytes of code
 */
#include <stdint.h>
#include <string.h>

#include "dumpulse.h"

enum {heartbeat_magic = 0xf1};
/* warning: ISO C restricts enumerator values to range of ‘int’ [-Wpedantic] */
#define MOD_ADLER 65521L

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

static void store_big_endian_u16(u8 *p, u16 v)
{
  p[0] = 0xff & (v >> 8);
  p[1] = 0xff & v;
}

static void store_big_endian_u32(u8 *p, u32 v)
{
  store_big_endian_u16(p, v >> 16);
  store_big_endian_u16(p+2, v);
}

static u32 fetch_big_endian_u32(u8 *p)
{
  return 0
    | (u32)p[0] << 24
    | (u32)p[1] << 16
    | (u32)p[2] << 8
    | (u32)p[3]
    ;
}

static u8 update_entry(dumpulse *p, u8 entry, u8 from, u8 value)
{
  u8 *item;
  if (entry >= dumpulse_n_variables) return 0;
  item = p->table + dumpulse_checksum_len + dumpulse_entry_size * entry;
  store_big_endian_u16(item, dumpulse_get_timestamp());
  item[2] = from;
  item[3] = value;
  return 1;
}

static u32 adler32(u8 *p, size_t len)
{
  u32 a = 1, b = 0;
  while (len--) {
    a += *p++;
    b += a;
    if (!(len & 0xf)) {
      if (a >= MOD_ADLER) a -= MOD_ADLER;
      if (b >= MOD_ADLER) b -= MOD_ADLER;
    }
  }
  return b << 16 | a;
}

static u8 process_heartbeat(dumpulse *p, u8 *data)
{
  u32 expected = fetch_big_endian_u32(data);
  u8 *payload = data + dumpulse_checksum_len;
  u32 checksum = adler32(payload,
                         dumpulse_timestamp_len
                         + dumpulse_id_len
                         + dumpulse_value_len);
  if (checksum != expected) return 0;
  return update_entry(p, payload[1], payload[2], payload[3]);
}

static void send_response(dumpulse *p, void *context)
{
  store_big_endian_u32(p->table, adler32(
    (u8*)p->table + dumpulse_checksum_len,
    sizeof(p->table) - dumpulse_checksum_len));
  dumpulse_send_packet(context, (char*)p->table, sizeof(p->table));
}

u8 dumpulse_process_packet(dumpulse *p, char *data, void *context)
{
  u8 *d = (u8*)data;
  if (heartbeat_magic == d[4]) {
    return process_heartbeat(p, d);
  } else if (0 == memcmp(d, "AreyouOK", 8)) {
    send_response(p, context);
    return 1;
  }
  return 0;
}
