/* Hand-built room illustration and original line icons, drawn by LVGL. SPDX-License-Identifier: MIT */
#include "lw_os_design.h"
#include <stdint.h>
static const lw_os_palette_t palettes[]={
    {0xfff7ee,0xf2e6e9,0xfffcf8,0xf3e8e8,0x51424a,0x81737c,0xe6d6d9,0xda879d,0x905668,0xd0ac8c,0xcde0dc,"Sakura"},
    {0x222940,0x3a3654,0x353c54,0x45465f,0xf7efe6,0xbeb8ce,0x55566c,0xbeafd9,0xd8bed8,0x8f7a85,0x374a6b,"Moonlight"},
    {0xf5f2df,0xe3e6d1,0xfbf9ee,0xe7e9d6,0x3f514a,0x78877a,0xd8deca,0x8ea990,0x526d5c,0xbb9978,0xd2e3ce,"Pixel Home"}
};
const lw_os_palette_t *lw_os_palette(unsigned i) { return &palettes[i<3 ? i : 0]; }
lv_obj_t *lw_os_box(lv_obj_t *p,int x,int y,int w,int h,uint32_t color,int radius,lv_opa_t opa)
{
    lv_obj_t *o=lv_obj_create(p); lv_obj_remove_style_all(o); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0); lv_obj_set_style_bg_opa(o,opa,0); lv_obj_set_style_radius(o,radius,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE); return o;
}
lv_obj_t *lw_os_label(lv_obj_t *p,const char *value,const lv_font_t *font,int x,int y,int w,uint32_t color)
{
    lv_obj_t *o=lv_label_create(p); lv_label_set_text(o,value); lv_obj_set_pos(o,x,y); if (w>0) lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_line_space(o,6,0); lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE); return o;
}
typedef struct { lv_layer_t *layer; lv_area_t area; float sx,sy; } painter_t;
static void rect(painter_t *p,int x,int y,int w,int h,uint32_t color,int radius,int opa)
{
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_color=lv_color_hex(color); d.bg_opa=(lv_opa_t)opa; d.radius=(int)(radius*p->sx);
    lv_area_t a={p->area.x1+(int)(x*p->sx),p->area.y1+(int)(y*p->sy),p->area.x1+(int)((x+w)*p->sx)-1,p->area.y1+(int)((y+h)*p->sy)-1}; lv_draw_rect(p->layer,&d,&a);
}
static void line(painter_t *p,int x1,int y1,int x2,int y2,uint32_t color,int width)
{
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.color=lv_color_hex(color); d.width=width; d.round_start=1; d.round_end=1;
    d.p1.x=p->area.x1+x1*p->sx; d.p1.y=p->area.y1+y1*p->sy; d.p2.x=p->area.x1+x2*p->sx; d.p2.y=p->area.y1+y2*p->sy; lv_draw_line(p->layer,&d);
}
static void arc(painter_t *p,int x,int y,int radius,int start,int end,uint32_t color,int width)
{
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d); d.center=(lv_point_t){p->area.x1+(int)(x*p->sx),p->area.y1+(int)(y*p->sy)};
    d.radius=(uint16_t)(radius*p->sx); d.start_angle=start; d.end_angle=end; d.color=lv_color_hex(color); d.width=width; d.rounded=1; lv_draw_arc(p->layer,&d);
}
static void icon_draw(lv_event_t *event)
{
    lv_obj_t *o=lv_event_get_target_obj(event); painter_t p={0}; p.layer=lv_event_get_layer(event); lv_obj_get_coords(o,&p.area);
    p.sx=(float)lv_obj_get_width(o)/48; p.sy=(float)lv_obj_get_height(o)/48;
    uint32_t encoded=(uint32_t)(uintptr_t)lv_event_get_user_data(event),c=encoded&0xffffff; unsigned kind=encoded>>24;
    int stroke=lv_obj_get_width(o)>=38 ? 3 : 2;
    switch (kind) {
    case OS_I_HOME:
        line(&p,8,24,24,10,c,stroke); line(&p,24,10,40,24,c,stroke); line(&p,12,22,12,39,c,stroke); line(&p,12,39,36,39,c,stroke); line(&p,36,39,36,22,c,stroke); line(&p,22,39,22,29,c,stroke); line(&p,22,29,28,29,c,stroke); break;
    case OS_I_ALBUM:
        rect(&p,9,8,30,33,c,5,255); rect(&p,12,11,24,27,0xfffaf4,2,255); rect(&p,15,24,18,11,c,1,255); arc(&p,28,18,3,0,359,c,3); line(&p,14,33,23,24,0xfffaf4,2); break;
    case OS_I_HEART:
        arc(&p,17,19,9,165,355,c,stroke); arc(&p,31,19,9,185,15,c,stroke); line(&p,8,21,24,38,c,stroke); line(&p,40,21,24,38,c,stroke); break;
    case OS_I_MUSIC:
        line(&p,20,32,20,13,c,stroke); line(&p,20,13,36,10,c,stroke); line(&p,36,10,36,29,c,stroke); rect(&p,11,29,10,8,c,4,255); rect(&p,27,26,10,8,c,4,255); break;
    case OS_I_GAME:
        rect(&p,6,14,36,23,c,9,255); rect(&p,9,17,30,17,0xfffaf4,6,255); line(&p,13,25,22,25,c,2); line(&p,17,21,17,29,c,2); rect(&p,29,21,4,4,c,2,255); rect(&p,34,26,4,4,c,2,255); break;
    case OS_I_SETTINGS:
        arc(&p,24,24,13,0,359,c,stroke); arc(&p,24,24,4,0,359,c,stroke); line(&p,24,6,24,10,c,4); line(&p,24,38,24,42,c,4); line(&p,6,24,10,24,c,4); line(&p,38,24,42,24,c,4); line(&p,11,11,14,14,c,3); line(&p,34,34,37,37,c,3); break;
    case OS_I_WIFI: arc(&p,24,38,28,226,314,c,stroke); arc(&p,24,38,19,226,314,c,stroke); arc(&p,24,38,10,226,314,c,stroke); rect(&p,22,35,5,5,c,3,255); break;
    case OS_I_SUN: arc(&p,24,24,8,0,359,c,stroke); line(&p,24,5,24,10,c,stroke); line(&p,24,38,24,43,c,stroke); line(&p,5,24,10,24,c,stroke); line(&p,38,24,43,24,c,stroke); line(&p,10,10,14,14,c,stroke); line(&p,34,34,38,38,c,stroke); line(&p,10,38,14,34,c,stroke); line(&p,34,14,38,10,c,stroke); break;
    case OS_I_VOLUME: rect(&p,7,19,9,12,c,2,255); line(&p,15,19,25,10,c,stroke); line(&p,25,10,25,38,c,stroke); line(&p,25,38,15,31,c,stroke); arc(&p,24,24,12,305,55,c,stroke); arc(&p,24,24,19,310,50,c,stroke); break;
    case OS_I_MOON: arc(&p,25,23,16,75,298,c,stroke); arc(&p,35,15,14,72,187,c,stroke); line(&p,29,38,38,29,c,stroke); break;
    case OS_I_BACK: line(&p,30,10,16,24,c,stroke); line(&p,16,24,30,38,c,stroke); break;
    case OS_I_CLOSE: line(&p,14,14,34,34,c,stroke); line(&p,34,14,14,34,c,stroke); break;
    case OS_I_LOCK: rect(&p,11,22,26,20,c,5,255); arc(&p,24,21,9,180,359,c,stroke); rect(&p,23,28,3,8,0xfffaf4,2,255); break;
    case OS_I_MAIL: rect(&p,6,12,36,26,c,4,255); line(&p,8,15,24,27,0xfffaf4,2); line(&p,24,27,40,15,0xfffaf4,2); break;
    case OS_I_PLAY: line(&p,18,10,36,24,c,stroke); line(&p,36,24,18,38,c,stroke); line(&p,18,38,18,10,c,stroke); break;
    case OS_I_CHECK: line(&p,11,24,21,34,c,stroke); line(&p,21,34,38,14,c,stroke); break;
    case OS_I_LEAF: arc(&p,24,24,14,185,350,c,stroke); arc(&p,24,24,14,5,170,c,stroke); line(&p,14,35,33,16,c,stroke); break;
    }
}
lv_obj_t *lw_os_icon(lv_obj_t *p,lw_os_icon_t id,int x,int y,int size,uint32_t c)
{
    lv_obj_t *o=lw_os_box(p,x,y,size,size,0,0,0);
    lv_obj_add_event_cb(o,icon_draw,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)(((uint32_t)id<<24)|(c&0xffffff))); return o;
}
static void plant(painter_t *p,int x,int y,int scale)
{
    rect(p,x-13*scale,y,26*scale,18*scale,0xc5907d,2,255); rect(p,x-16*scale,y,32*scale,5*scale,0xe3b39a,1,255);
    rect(p,x-2*scale,y-32*scale,4*scale,32*scale,0x687d66,0,255);
    rect(p,x-19*scale,y-27*scale,18*scale,11*scale,0x8fa17b,4*scale,255); rect(p,x+2*scale,y-39*scale,17*scale,12*scale,0xaab88f,4*scale,255);
    rect(p,x-13*scale,y-45*scale,13*scale,12*scale,0x7f977a,4*scale,255);
}
static void room_draw(lv_event_t *event)
{
    lv_obj_t *o=lv_event_get_target_obj(event); unsigned theme=(unsigned)(uintptr_t)lv_event_get_user_data(event); const lw_os_palette_t *c=lw_os_palette(theme);
    painter_t p={0}; p.layer=lv_event_get_layer(event); lv_obj_get_coords(o,&p.area); p.sx=(float)lv_obj_get_width(o)/1024; p.sy=(float)lv_obj_get_height(o)/600;
    rect(&p,0,0,1024,600,c->bg,0,255);
    /* The room is an original code illustration. No character/UI is baked into it. */
    rect(&p,344,66,640,402,theme==1 ? 0x30374e : 0xf3e8dc,28,255);
    rect(&p,344,346,640,122,theme==1 ? 0x655766 : 0xdec6af,0,255);
    rect(&p,344,342,640,5,c->wood,0,255);
    for (int y=369;y<468;y+=28) rect(&p,344,y,640,1,theme==1 ? 0x796774 : 0xcbb297,0,180);
    for (int row=0;row<4;row++) for (int x=366+row%2*54;x<984;x+=108) rect(&p,x,348+row*28,1,23,c->wood,0,180);
    rect(&p,508,96,244,212,theme==1 ? 0x777182 : 0xd5b99a,5,255);
    rect(&p,518,106,224,184,c->sky,2,255);
    if (theme==1) {
        rect(&p,678,127,33,33,0xf6deb3,16,255); rect(&p,690,120,30,30,c->sky,15,255);
        for (int i=0;i<16;i++) rect(&p,530+(i*53)%200,119+(i*31)%135,2,2,0xf2d9bd,0,255);
    } else {
        rect(&p,677,124,36,36,0xf3d393,18,255);
        rect(&p,524,229,218,61,0xafc4b2,0,255); rect(&p,524,247,103,43,0x97b2a3,5,255); rect(&p,627,236,111,54,0xa3bcab,7,255);
        rect(&p,555,168,53,13,0xf1eee1,7,255); rect(&p,576,160,29,19,0xf1eee1,9,255);
    }
    rect(&p,627,104,7,187,c->wood,0,255); rect(&p,515,194,230,6,c->wood,0,255);
    rect(&p,498,90,26,211,theme==1 ? 0xa299b2 : theme==2 ? 0xc0c9a0 : 0xe3b6bd,5,255); rect(&p,738,90,26,211,theme==1 ? 0xa299b2 : theme==2 ? 0xc0c9a0 : 0xe3b6bd,5,255);
    if (theme==2) for (int y=96;y<292;y+=12) { rect(&p,498,y,26,3,0xa7b38b,0,200); rect(&p,738,y,26,3,0xa7b38b,0,200); }
    for (int x=501;x<521;x+=6) rect(&p,x,94,2,198,theme==1 ? 0x8e859f : 0xd5a2ad,0,200);
    for (int x=741;x<761;x+=6) rect(&p,x,94,2,198,theme==1 ? 0x8e859f : 0xd5a2ad,0,200);
    rect(&p,493,296,277,10,c->wood,2,255);
    /* Sofa upholstery, feet, folded blanket and a small side table. */
    rect(&p,378,314,177,59,0x9a7d79,12,80); rect(&p,389,276,151,70,theme==1 ? 0x9a859c : theme==2 ? 0x94a68e : 0xccabb6,10,255);
    rect(&p,379,310,173,46,theme==1 ? 0x8e7a92 : theme==2 ? 0xa7b49b : 0xddbdc5,11,255); rect(&p,390,316,151,20,theme==1 ? 0xa794ab : theme==2 ? 0xbac6a6 : 0xebcfd2,7,255);
    rect(&p,389,354,8,19,c->wood,0,255); rect(&p,534,354,8,19,c->wood,0,255);
    rect(&p,402,292,46,30,0xf3e5d4,7,255); rect(&p,465,314,59,31,0xc5c7aa,3,255);
    for (int x=472;x<524;x+=9) rect(&p,x,315,3,29,0xa6b296,0,255);
    rect(&p,806,234,128,8,c->wood,1,255); rect(&p,821,242,5,22,c->wood,0,255); rect(&p,913,242,5,22,c->wood,0,255);
    plant(&p,895,216,1); rect(&p,822,211,11,23,0xd89b94,0,255); rect(&p,835,204,9,30,0xb3b599,0,255); rect(&p,846,215,18,19,0xe5c78f,0,255);
    rect(&p,823,111,77,78,c->wood,2,255); rect(&p,829,117,65,66,theme==1 ? 0x515a72 : 0xf9efe0,0,255);
    rect(&p,843,153,14,15,0xb98e9c,4,255); rect(&p,859,143,20,25,0xb98e9c,5,255); rect(&p,846,168,31,4,0xa8b294,0,255);
    rect(&p,858,354,88,14,c->wood,7,255); rect(&p,870,368,7,46,c->wood,0,255); rect(&p,929,368,7,46,c->wood,0,255);
    plant(&p,903,337,1); rect(&p,867,340,20,12,0xffecd1,3,255);
    rect(&p,421,420,468,32,theme==1 ? 0xa198ac : 0xf1e3ce,16,220);
    rect(&p,435,424,440,24,theme==1 ? 0x8b829c : 0xe4d1b8,12,150);
    /* A few clean pixel details give scale without texturing the entire screen. */
    for (int x=436;x<873;x+=12) rect(&p,x,435,5,2,theme==1 ? 0xb7a9bb : 0xcab69e,0,120);
    plant(&p,952,395,1);
    rect(&p,0,488,1024,112,c->lower,0,155);
}
lv_obj_t *lw_os_wallpaper(lv_obj_t *p,unsigned theme,int x,int y,int w,int h)
{
    lv_obj_t *o=lw_os_box(p,x,y,w,h,0,0,0); lv_obj_add_event_cb(o,room_draw,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)theme); return o;
}
static void move(void *o,int32_t y) { lv_obj_set_style_translate_y(o,y,0); }
void lw_os_animate(lv_obj_t *o,int offset,uint32_t duration)
{
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a,o); lv_anim_set_exec_cb(&a,move); lv_anim_set_values(&a,offset,0);
    lv_anim_set_duration(&a,duration); lv_anim_set_path_cb(&a,lv_anim_path_ease_out); lv_anim_start(&a);
}
