#define _GNU_SOURCE
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <poll.h>
#include <errno.h>
#include "xdg-shell.h"
#include "text-input.h"

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct wl_surface *surface;
static struct xdg_wm_base *wm;
static struct xdg_surface *xdg;
static struct zwp_text_input_manager_v3 *manager;
static struct zwp_text_input_v3 *input;
static int width=700, height=450, active, configured, running=1, pending;
static int popup_style;
static unsigned serial;
static long long replay_due;
static long long now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static char *preedit, *next_preedit;
static FILE *logfile;
static void event(const char *fmt, ...) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    fprintf(logfile,"mono_ns=%lld ",(long long)t.tv_sec*1000000000LL+t.tv_nsec);
    va_list args; va_start(args,fmt); vfprintf(logfile,fmt,args); va_end(args);
    fputc('\n',logfile); fflush(logfile);
}
static void rectangle(const char *reason) {
    if(!active || !configured) return;
    zwp_text_input_v3_set_cursor_rectangle(input,32,height-44,2,24);
    zwp_text_input_v3_commit(input);
    event("rectangle reason=%s x=32 y=%d w=2 h=24 serial=%u",reason,height-44,++serial);
}
struct image { struct wl_buffer *buffer; void *data; size_t size; };
static void released(void *data,struct wl_buffer *buffer) {
    struct image *im=data; wl_buffer_destroy(buffer); munmap(im->data,im->size); free(im);
}
static const struct wl_buffer_listener buffer_listener={released};
static void label(cairo_t *cr,double x,double y,const char *s) {
    cairo_move_to(cr,x,y); cairo_show_text(cr,s);
}
static void draw(void) {
    if(!configured) return;
    struct image *im=calloc(1,sizeof(*im)); if(!im) exit(2);
    im->size=(size_t)width*height*4;
    int fd=memfd_create("input-popup-probe",MFD_CLOEXEC);
    if(fd<0 || ftruncate(fd,(off_t)im->size)<0) exit(2);
    im->data=mmap(NULL,im->size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(im->data==MAP_FAILED) exit(2);
    struct wl_shm_pool *pool=wl_shm_create_pool(shm,fd,(int)im->size);
    im->buffer=wl_shm_pool_create_buffer(pool,0,width,height,width*4,WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool); close(fd);
    wl_buffer_add_listener(im->buffer,&buffer_listener,im);
    cairo_surface_t *cs=cairo_image_surface_create_for_data(im->data,CAIRO_FORMAT_ARGB32,width,height,width*4);
    cairo_t *cr=cairo_create(cs);
    cairo_set_source_rgb(cr,.08,.09,.13); cairo_paint(cr);
    cairo_select_font_face(cr,"sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr,19); cairo_set_source_rgb(cr,.85,.88,.95);
    label(cr,24,36,"Wayland input-popup probe");
    cairo_set_font_size(cr,15);
    label(cr,24,68,"The test input method displays its popup automatically.");
    label(cr,24,94,"Press F6 or click to resend the identical cursor rectangle.");
    label(cr,24,120,"Resend does not redraw, resize, or change the preedit.");
    cairo_set_source_rgb(cr,.18,.21,.29); cairo_rectangle(cr,24,height-52,width-48,36); cairo_fill(cr);
    cairo_set_source_rgb(cr,.7,.82,1); cairo_rectangle(cr,32,height-44,2,24); cairo_fill(cr);
    cairo_set_font_size(cr,18); label(cr,42,height-25,preedit?preedit:"");
    if(popup_style) {
        cairo_set_source_rgb(cr,.1,.6,.6); cairo_paint(cr);
        cairo_set_source_rgb(cr,1,1,1); cairo_set_font_size(cr,20);
        label(cr,16,40,"SHM input popup - 411 x 65");
    }
    cairo_destroy(cr); cairo_surface_flush(cs); cairo_surface_destroy(cs);
    wl_surface_attach(surface,im->buffer,0,0); wl_surface_damage(surface,0,0,width,height); wl_surface_commit(surface);
    event("surface_commit width=%d height=%d",width,height);
}
static void ti_enter(void *d,struct zwp_text_input_v3 *i,struct wl_surface *s) {
    if(s!=surface) return;
    active=1;
    zwp_text_input_v3_enable(i);
    zwp_text_input_v3_set_content_type(i,0,ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    zwp_text_input_v3_set_surrounding_text(i,"",0,0);
    if(configured) rectangle("enter"); else {zwp_text_input_v3_commit(i); ++serial;}
    event("enter serial=%u",serial);
}
static void ti_leave(void *d,struct zwp_text_input_v3 *i,struct wl_surface *s) {
    active=0; replay_due=0; zwp_text_input_v3_disable(i); zwp_text_input_v3_commit(i); ++serial; event("leave");
}
static void ti_preedit(void *d,struct zwp_text_input_v3 *i,const char *text,int32_t begin,int32_t end) {
    free(next_preedit); next_preedit=strdup(text?text:""); pending=1;
    event("preedit bytes=%zu cursor_begin=%d cursor_end=%d",strlen(text?text:""),begin,end);
}
static void ti_commit(void *d,struct zwp_text_input_v3 *i,const char *text) {event("commit_string bytes=%zu",strlen(text?text:""));}
static void ti_delete(void *d,struct zwp_text_input_v3 *i,uint32_t before,uint32_t after) {event("delete before=%u after=%u",before,after);}
static void ti_done(void *d,struct zwp_text_input_v3 *i,uint32_t value) {
    event("done serial=%u client_serial=%u",value,serial);
    if(pending) {
        int first = (!preedit || !*preedit) && next_preedit && strlen(next_preedit)==1;
        replay_due=0;
        free(preedit); preedit=next_preedit; next_preedit=NULL; pending=0; draw();
        if(first && active && !getenv("MIRIX_PROBE_NO_REPLAY")) {
            replay_due=now_ms()+2000;
            event("same_rectangle_probe_armed delay_ms=2000");
        }
    }
}
static const struct zwp_text_input_v3_listener input_listener={.enter=ti_enter,.leave=ti_leave,.preedit_string=ti_preedit,.commit_string=ti_commit,.delete_surrounding_text=ti_delete,.done=ti_done};
static void keymap(void *d,struct wl_keyboard *k,uint32_t f,int32_t fd,uint32_t size) {close(fd);}
static void key_enter(void *d,struct wl_keyboard *k,uint32_t s,struct wl_surface *w,struct wl_array *keys) {}
static void key_leave(void *d,struct wl_keyboard *k,uint32_t s,struct wl_surface *w) {}
static void key(void *d,struct wl_keyboard *k,uint32_t s,uint32_t time,uint32_t key,uint32_t state) {
    if(state==WL_KEYBOARD_KEY_STATE_PRESSED && key==KEY_F6) rectangle("manual_key");
}
static void modifiers(void *d,struct wl_keyboard *k,uint32_t s,uint32_t a,uint32_t b,uint32_t c,uint32_t g) {}
static const struct wl_keyboard_listener keyboard_listener={.keymap=keymap,.enter=key_enter,.leave=key_leave,.key=key,.modifiers=modifiers};
static void ptr_enter(void *d,struct wl_pointer *p,uint32_t s,struct wl_surface *w,wl_fixed_t x,wl_fixed_t y) {}
static void ptr_leave(void *d,struct wl_pointer *p,uint32_t s,struct wl_surface *w) {}
static void motion(void *d,struct wl_pointer *p,uint32_t t,wl_fixed_t x,wl_fixed_t y) {}
static void button(void *d,struct wl_pointer *p,uint32_t s,uint32_t t,uint32_t b,uint32_t state) {
    if(b==BTN_LEFT && state==WL_POINTER_BUTTON_STATE_PRESSED) rectangle("manual_click");
}
static void axis(void *d,struct wl_pointer *p,uint32_t t,uint32_t a,wl_fixed_t v) {}
static const struct wl_pointer_listener pointer_listener={.enter=ptr_enter,.leave=ptr_leave,.motion=motion,.button=button,.axis=axis};
static void capabilities(void *d,struct wl_seat *s,uint32_t caps) {
    static struct wl_keyboard *k; static struct wl_pointer *p;
    if((caps&WL_SEAT_CAPABILITY_KEYBOARD) && !k) {k=wl_seat_get_keyboard(s);wl_keyboard_add_listener(k,&keyboard_listener,NULL);}
    if((caps&WL_SEAT_CAPABILITY_POINTER) && !p) {p=wl_seat_get_pointer(s);wl_pointer_add_listener(p,&pointer_listener,NULL);}
}
static const struct wl_seat_listener seat_listener={.capabilities=capabilities};
static void ping(void *d,struct xdg_wm_base *w,uint32_t s) {xdg_wm_base_pong(w,s);}
static const struct xdg_wm_base_listener wm_listener={ping};
static void configure(void *d,struct xdg_surface *s,uint32_t value) {
    xdg_surface_ack_configure(s,value); configured=1; replay_due=0;
    event("configure serial=%u width=%d height=%d",value,width,height); draw(); rectangle("configure");
}
static const struct xdg_surface_listener surface_listener={configure};
static void top_configure(void *d,struct xdg_toplevel *t,int32_t w,int32_t h,struct wl_array *states) {
    if(w>0) width=w;
    if(h>0) height=h;
}
static void top_close(void *d,struct xdg_toplevel *t) {running=0;}
static const struct xdg_toplevel_listener top_listener={.configure=top_configure,.close=top_close};
static void global(void *d,struct wl_registry *r,uint32_t name,const char *iface,uint32_t version) {
    if(!strcmp(iface,"wl_compositor")) compositor=wl_registry_bind(r,name,&wl_compositor_interface,1);
    else if(!strcmp(iface,"wl_shm")) shm=wl_registry_bind(r,name,&wl_shm_interface,1);
    else if(!strcmp(iface,"xdg_wm_base")) {wm=wl_registry_bind(r,name,&xdg_wm_base_interface,1);xdg_wm_base_add_listener(wm,&wm_listener,NULL);}
    else if(!strcmp(iface,"wl_seat")) {seat=wl_registry_bind(r,name,&wl_seat_interface,1);wl_seat_add_listener(seat,&seat_listener,NULL);}
    else if(!strcmp(iface,"zwp_text_input_manager_v3")) manager=wl_registry_bind(r,name,&zwp_text_input_manager_v3_interface,1);
}
static void removed(void *d,struct wl_registry *r,uint32_t n) {}
static const struct wl_registry_listener registry_listener={global,removed};
int main(void) {
    logfile=fopen("client.log","a"); if(!logfile) return 1;
    display=wl_display_connect(NULL); if(!display) {perror("wl_display_connect");return 1;}
    struct wl_registry *registry=wl_display_get_registry(display);wl_registry_add_listener(registry,&registry_listener,NULL);
    if(wl_display_roundtrip(display)<0 || !compositor || !shm || !wm || !seat || !manager) {event("missing required globals");return 1;}
    input=zwp_text_input_manager_v3_get_text_input(manager,seat);zwp_text_input_v3_add_listener(input,&input_listener,NULL);
    surface=wl_compositor_create_surface(compositor);xdg=xdg_wm_base_get_xdg_surface(wm,surface);xdg_surface_add_listener(xdg,&surface_listener,NULL);
    struct xdg_toplevel *top=xdg_surface_get_toplevel(xdg);xdg_toplevel_add_listener(top,&top_listener,NULL);
    xdg_toplevel_set_title(top,"Wayland input-popup probe");xdg_toplevel_set_app_id(top,"input-popup-probe");
    wl_surface_commit(surface);event("start pid=%d",getpid());
    while(running) {
        while(wl_display_prepare_read(display)!=0) {
            if(wl_display_dispatch_pending(display)<0) {running=0;break;}
        }
        if(!running) break;
        int flushed=wl_display_flush(display);
        if(flushed<0 && errno!=EAGAIN) {wl_display_cancel_read(display);break;}
        int timeout=-1;
        if(replay_due) {long long left=replay_due-now_ms();timeout=left>0?(int)left:0;}
        struct pollfd fd={.fd=wl_display_get_fd(display),.events=POLLIN | (flushed<0?POLLOUT:0)};
        int result=poll(&fd,1,timeout);
        if(result>0 && (fd.revents&POLLIN)) {
            if(wl_display_read_events(display)<0) break;
        } else wl_display_cancel_read(display);
        if(result<0 && errno!=EINTR) break;
        if(fd.revents&(POLLERR|POLLHUP|POLLNVAL)) break;
        if(wl_display_dispatch_pending(display)<0) break;
        if(replay_due && now_ms()>=replay_due) {
            replay_due=0;
            if(active && preedit && strlen(preedit)==1) rectangle("delayed_same_rect");
        }
    }
    event("exit");wl_display_disconnect(display);fclose(logfile);return 0;
}
