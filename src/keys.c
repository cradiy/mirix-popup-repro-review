#define _GNU_SOURCE
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "virtual-keyboard.h"
static struct wl_seat *seat;
static struct zwp_virtual_keyboard_manager_v1 *manager;
static void global(void *d,struct wl_registry *r,uint32_t name,const char *iface,uint32_t version) {
 if(!strcmp(iface,"wl_seat"))seat=wl_registry_bind(r,name,&wl_seat_interface,1);
 if(!strcmp(iface,"zwp_virtual_keyboard_manager_v1"))manager=wl_registry_bind(r,name,&zwp_virtual_keyboard_manager_v1_interface,1);
}
static void removed(void *d,struct wl_registry *r,uint32_t name) {}
static const struct wl_registry_listener listener={global,removed};
int main(void) {
 const char *allowed=getenv("MIRIX_PROBE_DISPLAY"), *actual=getenv("WAYLAND_DISPLAY");
 if(!allowed || !actual || strcmp(allowed,actual))return 2;
 struct wl_display *display=wl_display_connect(NULL);if(!display)return 2;
 struct wl_registry *r=wl_display_get_registry(display);wl_registry_add_listener(r,&listener,NULL);
 if(wl_display_roundtrip(display)<0 || !manager || !seat)return 2;
 struct zwp_virtual_keyboard_v1 *keyboard=zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager,seat);
 struct xkb_context *context=xkb_context_new(XKB_CONTEXT_NO_FLAGS);
 struct xkb_rule_names names={.layout="us"};
 struct xkb_keymap *map=xkb_keymap_new_from_names(context,&names,XKB_KEYMAP_COMPILE_NO_FLAGS);
 if(!map)return 2;
 char *text=xkb_keymap_get_as_string(map,XKB_KEYMAP_FORMAT_TEXT_V1);size_t size=strlen(text)+1;
 int fd=memfd_create("probe-keymap",MFD_CLOEXEC);if(fd<0 || write(fd,text,size)!=(ssize_t)size)return 2;
 zwp_virtual_keyboard_v1_keymap(keyboard,WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,fd,size);close(fd);free(text);
 xkb_keymap_unref(map);xkb_context_unref(context);
 if(wl_display_roundtrip(display)<0)return 2;
 char line[32];
 while(fgets(line,sizeof(line),stdin)) {
  unsigned code=0;if(!strcmp(line,"s\n"))code=31;else if(!strcmp(line,"clear\n"))code=14;else if(!strcmp(line,"replay\n"))code=64;else return 2;
  struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);uint32_t ms=(uint32_t)(t.tv_sec*1000+t.tv_nsec/1000000);
  zwp_virtual_keyboard_v1_key(keyboard,ms,code,WL_KEYBOARD_KEY_STATE_PRESSED);
  zwp_virtual_keyboard_v1_key(keyboard,ms+1,code,WL_KEYBOARD_KEY_STATE_RELEASED);
  if(wl_display_roundtrip(display)<0)return 2;
  puts("sent");fflush(stdout);
 }
 zwp_virtual_keyboard_v1_destroy(keyboard);wl_display_flush(display);wl_display_disconnect(display);return 0;
}
