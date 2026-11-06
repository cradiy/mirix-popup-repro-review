#define main text_probe_main
#include "probe.c"
#undef main
#include "input-method.h"
static struct zwp_input_method_manager_v2 *im_manager;
static struct zwp_input_method_v2 *im;
static unsigned im_serial;
static int im_pending_active, im_active;
static long long show_due, paint_due, hide_due;
static int cycled;
static void im_activate(void *d,struct zwp_input_method_v2 *v) {im_pending_active=1;event("activate");}
static void im_deactivate(void *d,struct zwp_input_method_v2 *v) {im_pending_active=0;event("deactivate");}
static void im_surround(void *d,struct zwp_input_method_v2 *v,const char *text,uint32_t c,uint32_t a) {}
static void im_cause(void *d,struct zwp_input_method_v2 *v,uint32_t cause) {}
static void im_content(void *d,struct zwp_input_method_v2 *v,uint32_t hint,uint32_t purpose) {}
static void im_done(void *d,struct zwp_input_method_v2 *v) {
    ++im_serial;
    if(im_pending_active && !im_active) {show_due=now_ms()+500; event("show_armed delay_ms=500");}
    if(!im_pending_active && im_active) {
        show_due=paint_due=0; wl_surface_attach(surface,NULL,0,0);wl_surface_commit(surface);event("hide");
    }
    im_active=im_pending_active;
}
static void unavailable(void *d,struct zwp_input_method_v2 *v) {event("unavailable");running=0;}
static const struct zwp_input_method_v2_listener im_listener={
 .activate=im_activate,.deactivate=im_deactivate,.surrounding_text=im_surround,
 .text_change_cause=im_cause,.content_type=im_content,.done=im_done,.unavailable=unavailable};
static void rect(void *d,struct zwp_input_popup_surface_v2 *p,int32_t x,int32_t y,int32_t w,int32_t h) {
    event("popup_rectangle x=%d y=%d w=%d h=%d",x,y,w,h);
}
static const struct zwp_input_popup_surface_v2_listener pop_listener={.text_input_rectangle=rect};
static void im_global(void *d,struct wl_registry *r,uint32_t name,const char *iface,uint32_t version) {
    if(!strcmp(iface,"zwp_input_method_manager_v2")) im_manager=wl_registry_bind(r,name,&zwp_input_method_manager_v2_interface,1);
    else if(!strcmp(iface,"wl_compositor")) compositor=wl_registry_bind(r,name,&wl_compositor_interface,1);
    else if(!strcmp(iface,"wl_shm")) shm=wl_registry_bind(r,name,&wl_shm_interface,1);
    else if(!strcmp(iface,"wl_seat")) seat=wl_registry_bind(r,name,&wl_seat_interface,1);
}
static const struct wl_registry_listener im_registry_listener={im_global,removed};
int main(void) {
    logfile=fopen("input-method.log","a");if(!logfile)return 1;
    display=wl_display_connect(NULL);if(!display)return 1;
    struct wl_registry *r=wl_display_get_registry(display);wl_registry_add_listener(r,&im_registry_listener,NULL);
    if(wl_display_roundtrip(display)<0 || !im_manager || !compositor || !shm || !seat)return 2;
    im=zwp_input_method_manager_v2_get_input_method(im_manager,seat);zwp_input_method_v2_add_listener(im,&im_listener,NULL);
    surface=wl_compositor_create_surface(compositor);
    struct zwp_input_popup_surface_v2 *pop=zwp_input_method_v2_get_input_popup_surface(im,surface);
    zwp_input_popup_surface_v2_add_listener(pop,&pop_listener,NULL);
    configured=1;width=411;height=65;popup_style=1;cycled=getenv("MIRIX_IME_NO_CYCLE")!=NULL; event("start pid=%d popup=411x65",getpid());
    while(running) {
        while(wl_display_prepare_read(display)!=0)if(wl_display_dispatch_pending(display)<0){running=0;break;}
        if(!running)break;
        int flushed=wl_display_flush(display);
        if(flushed<0 && errno!=EAGAIN){wl_display_cancel_read(display);break;}
        long long due=show_due?show_due:paint_due;
        long long left=due-now_ms();
        int timeout=due?(left>0?(int)left:0):-1;
        struct pollfd fd={.fd=wl_display_get_fd(display),.events=POLLIN|(flushed<0?POLLOUT:0)};
        int result=poll(&fd,1,timeout);
        if(result>0 && (fd.revents&POLLIN)){if(wl_display_read_events(display)<0)break;}
        else wl_display_cancel_read(display);
        if(result<0 && errno!=EINTR)break;
        if(fd.revents&(POLLERR|POLLHUP|POLLNVAL))break;
        if(wl_display_dispatch_pending(display)<0)break;
        if(im_active && show_due && now_ms()>=show_due) {
            show_due=0;
            zwp_input_method_v2_set_preedit_string(im,"s",1,1);
            zwp_input_method_v2_commit(im,im_serial);
            draw();paint_due=now_ms()+100;
            if(!cycled) hide_due=now_ms()+5500;
            event("show serial=%u",im_serial);
        } else if(im_active && hide_due && now_ms()>=hide_due) {
            hide_due=0;cycled=1;paint_due=0;show_due=now_ms()+1000;
            zwp_input_method_v2_set_preedit_string(im,"",0,0);
            zwp_input_method_v2_commit(im,im_serial);
            wl_surface_attach(surface,NULL,0,0);wl_surface_commit(surface);event("cycle_hide");
        } else if(im_active && paint_due && now_ms()>=paint_due) {
            draw();paint_due=now_ms()+100;
        }
    }
    wl_display_disconnect(display);fclose(logfile);return 0;
}
