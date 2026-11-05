#include "lw_skin.h"
#include <stdio.h>
#include "lw_assets.h"
#include "lw_os_design.h"
#include "lw_ambience.h"
typedef struct {int x,y,w,h;} rect_t;
static const rect_t rects[SK_COUNT]={
 {0,0,512,44},{0,75,316,45},{316,75,196,28},
 {0,120,112,44},{112,120,112,44},{224,120,112,44},
 {336,120,64,44},{400,120,56,44},{456,120,20,58},
 {0,178,148,136},{0,314,480,220},
 {152,178,128,128},{284,178,96,48},{380,178,96,48},{284,230,96,48},{492,178,16,128},{380,230,32,40}};
static lw_asset_lease_t *atlas;static unsigned refs;

/* One art object draws the whole nine-slice.
 *
 * The old shape was a container box plus an lv_image per slice, so a panel with
 * a 16 px border was eighteen widgets: the box was only there to clip the scaled
 * atlas down to the piece, because lv_image can only scale a whole image. The
 * control centre built 103 slices that way and paid 206 object creations plus
 * four setters each on every open.
 *
 * A list of source/dest rectangles costs nothing to build and lets one object
 * paint every piece in a single DRAW_MAIN: each block is an lv_draw_image of the
 * same atlas with its own image_area, scale and destination - the clip comes
 * from the intersection with the draw's own area, which is exactly what the box
 * used to do.
 *
 * The rect math has to match what the widget picked before byte for byte;
 * `tools/shot_diff.py` against the 156 host screenshots is the referee. */
#define ART_MAX_BLOCKS 9
typedef struct {
    int count;
    struct { int16_t x,y,w,h; } src[ART_MAX_BLOCKS];   /* source rect inside the atlas */
    struct { int16_t dx,dy,dw,dh; } dst[ART_MAX_BLOCKS]; /* destination inside the object */
    int atlas_w, atlas_h;
    int border;                  /* nine-slice border of this object, for state swaps */
    lw_skin_id_t base, selected, pressed;
} art_t;

static const lv_image_dsc_t *atlas_image(void)
{
    if(!atlas && !lw_assets_find("v43skin.atlas"))return NULL;
    if(!atlas)atlas=lw_asset_acquire("v43skin.atlas",0);
    return atlas ? lw_asset_image(atlas) : NULL;
}

/* Draw every piece of one art object into a layer. The layer may be the screen
 * (normal path) or an offscreen canvas (bake path) - both take absolute screen
 * coordinates, so the geometry below is shared. */
static void art_pieces(lv_obj_t *obj,lv_layer_t *layer,const art_t *a,const lv_area_t *co)
{
    const lv_image_dsc_t *img=atlas_image();
    if(!img)return;
    /* Each piece keeps the clip its box used to provide. Narrowing the layer's
     * clip for the duration of one draw is the same thing lv_refr does per
     * object (lv_refr.c sets layer->_clip_area before sending DRAW_MAIN), so the
     * pieces never bleed into each other. */
    const lv_area_t outer=layer->_clip_area;
    /* One style lookup for the whole object: opa and the night/dusk recolor live
     * on the object, and lv_obj_init_draw_image_dsc is what folds them into the
     * draw descriptor. */
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.base.layer=layer;
    lv_obj_init_draw_image_dsc(obj,LV_PART_MAIN,&dsc);
    if(dsc.opa<=LV_OPA_MIN)return;
    dsc.src=img;
    dsc.antialias=false;
    dsc.pivot.x=0;dsc.pivot.y=0;
    for(int k=0;k<a->count;k++){
        const int16_t sx=a->src[k].x, sy=a->src[k].y, sw=a->src[k].w, sh=a->src[k].h;
        const int16_t dx=a->dst[k].dx, dy=a->dst[k].dy, dw=a->dst[k].dw, dh=a->dst[k].dh;
        if(sw<1 || sh<1 || dw<1 || dh<1)continue;
        lv_area_t clip;
        clip.x1=co->x1+dx; clip.y1=co->y1+dy;
        clip.x2=clip.x1+dw-1; clip.y2=clip.y1+dh-1;
        if(clip.x1<outer.x1)clip.x1=outer.x1;
        if(clip.y1<outer.y1)clip.y1=outer.y1;
        if(clip.x2>outer.x2)clip.x2=outer.x2;
        if(clip.y2>outer.y2)clip.y2=outer.y2;
        if(clip.x1>clip.x2 || clip.y1>clip.y2)continue;
        const uint32_t scale_x=(uint32_t)((unsigned)dw*256u/(unsigned)sw);
        const uint32_t scale_y=(uint32_t)((unsigned)dh*256u/(unsigned)sh);
        dsc.scale_x=scale_x;dsc.scale_y=scale_y;
        /* Place the whole atlas so that its (sx,sy) pixel lands on the block's
         * destination origin, then let the clip above cut out just this piece -
         * the same arithmetic lv_image did when the pivot was (0,0) and the
         * image sat at -source*scale/256 inside its own box. */
        const int32_t ox=co->x1+dx-(sx*(int32_t)scale_x>>8);
        const int32_t oy=co->y1+dy-(sy*(int32_t)scale_y>>8);
        lv_area_set(&dsc.image_area,ox,oy,ox+a->atlas_w-1,oy+a->atlas_h-1);
        layer->_clip_area=clip;
        lv_draw_image(layer,&dsc,&dsc.image_area);
    }
    layer->_clip_area=outer;
}

/* ------------------------------------------------------------------------- */
/* Baked panels.                                                            */
/*                                                                          */
/* Nine slices cost their whole destination area through                */
/* transform_and_recolor(): measured 3.8 us/px on the panel (807 ms for the  */
/* 468x454 middle block alone), while a 1:1 blit of the same pixels runs at  */
/* ~0.25 us/px. So each panel size is rendered once into an offscreen buffer */
/* and afterwards drawn as one 1:1 image.                                    */
/*                                                                          */
/* The bake buffer is ARGB8888 (canvas), then converted by hand to RGB565A8  */
/* because that is the only format that takes LVGL's fast image path and     */
/* because a canvas of RGB565A8 leaves the alpha plane at zero.              */
#define PANEL_CACHE 32
typedef struct {
    lv_image_dsc_t dsc;
    uint8_t *px;
    lw_skin_id_t id;
    int w,h,phase;
} baked_t;
static baked_t s_baked[PANEL_CACHE];

static unsigned s_bake_hit, s_bake_miss, s_bake_skip;

unsigned lw_skin_bake_stats(unsigned *hits, unsigned *misses)
{
    *hits = s_bake_hit;
    *misses = s_bake_miss;
    return s_bake_hit + s_bake_miss;
}

static unsigned s_bake_clock;

static int skin_phase(void)
{
    return lw_ambience_phase()>=LW_NIGHT ? 1 : 0;
}

/* ARGB8888 (B,G,R,A in memory) to RGB565A8, undoing the premultiply the
 * canvas blend left behind. */
static void bake_convert(uint8_t *dst,const uint8_t *src,int w,int h)
{
    for(int y=0;y<h;y++){
        const uint8_t *srow=src+(size_t)y*(size_t)w*4u;
        uint16_t *drow=(uint16_t*)(void*)(dst+(size_t)y*(size_t)w*2u);
        uint8_t *arow=dst+(size_t)w*(size_t)h*2u+(size_t)y*(size_t)w;
        for(int x=0;x<w;x++){
            const uint8_t *q=srow+(size_t)x*4u;
            unsigned a=q[3], r=q[2], g=q[1], b=q[0];
            if(a && a<255){
                r=(r*255u+a/2u)/a; g=(g*255u+a/2u)/a; b=(b*255u+a/2u)/a;
                if(r>255){r=255;}
                if(g>255){g=255;}
                if(b>255){b=255;}
            }
            /* Round to 5/6/5 instead of truncating: truncation biased every
               baked pixel one step dark, which showed up as a worst-case 9. */
            {
                const unsigned r5=((unsigned)r*31u+127u)/255u;
                const unsigned g6=((unsigned)g*63u+127u)/255u;
                const unsigned b5=((unsigned)b*31u+127u)/255u;
                drow[x]=(uint16_t)((r5<<11)|(g6<<5)|b5);
            }
            arow[x]=(uint8_t)a;
        }
    }
}

static const lv_image_dsc_t *art_baked(lv_obj_t *obj,const art_t *a,const lv_area_t *co)
{
    const int w=lv_area_get_width(co), h=lv_area_get_height(co);
#ifndef LW_SKIN_BAKE
#define LW_SKIN_BAKE 0
#endif
    if(!LW_SKIN_BAKE){ s_bake_skip++; return NULL; }
    if(a->count!=9){ s_bake_skip++; return NULL; }   /* only nine-slice panels are worth a bake */
    if(w<2 || h<2 || w>640 || h>600)return NULL;
    const int phase=skin_phase();
    for(unsigned i=0;i<PANEL_CACHE;i++){
        if(s_baked[i].px && s_baked[i].id==a->base && s_baked[i].w==w && s_baked[i].h==h && s_baked[i].phase==phase)
            { s_bake_hit++; return &s_baked[i].dsc; }
    }
    baked_t *bk=NULL;
    for(unsigned i=0;i<PANEL_CACHE;i++){ if(!s_baked[i].px){ bk=&s_baked[i]; break; } }
    if(!bk) bk=&s_baked[s_bake_clock++%PANEL_CACHE];
    if(bk->px){ lv_free(bk->px); bk->px=NULL; }

    const size_t argb_bytes=(size_t)w*(size_t)h*4u;
    const size_t out_bytes=(size_t)w*(size_t)h*2u+(size_t)w*(size_t)h;
    uint8_t *argb=lv_malloc(argb_bytes);
    uint8_t *out=lv_malloc(out_bytes);
    if(!argb || !out){ if(argb)lv_free(argb); if(out)lv_free(out); return NULL; }
    lv_memset(argb,0,argb_bytes);

    lv_obj_t *cv=lv_canvas_create(lv_obj_get_parent(obj));
    lv_obj_set_pos(cv,co->x1,co->y1);
    lv_obj_set_size(cv,w,h);
    lv_obj_remove_flag(cv,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(cv,0,0);
    lv_obj_set_style_border_width(cv,0,0);
    lv_obj_set_style_bg_opa(cv,0,0);
    lv_canvas_set_buffer(cv,argb,w,h,LV_COLOR_FORMAT_ARGB8888);
    lv_layer_t layer;
    lv_canvas_init_layer(cv,&layer);
    lv_area_t local;
    local.x1=0; local.y1=0; local.x2=w-1; local.y2=h-1;
    art_pieces(obj,&layer,a,&local);
    lv_canvas_finish_layer(cv,&layer);
    lv_obj_delete(cv);

    lv_memset(out,0,out_bytes);
    bake_convert(out,argb,w,h);
    lv_free(argb);

    s_bake_miss++;
    printf("skinbake: hit=%u miss=%u skip=%u\n", s_bake_hit, s_bake_miss, s_bake_skip);
    bk->px=out; bk->id=a->base; bk->w=w; bk->h=h; bk->phase=phase;
    lv_memset(&bk->dsc,0,sizeof(bk->dsc));
    bk->dsc.header.magic=LV_IMAGE_HEADER_MAGIC;
    bk->dsc.header.cf=LV_COLOR_FORMAT_RGB565A8;
    bk->dsc.header.w=(uint32_t)w;
    bk->dsc.header.h=(uint32_t)h;
    bk->dsc.header.stride=(uint32_t)w*2u;
    bk->dsc.data_size=(uint32_t)out_bytes;
    bk->dsc.data=out;
    return &bk->dsc;
}

static void art_draw(lv_event_t *e)
{
    lv_obj_t *obj=lv_event_get_target_obj(e);
    const art_t *a=lv_obj_get_user_data(obj);
    if(!a)return;
    lv_layer_t *layer=lv_event_get_layer(e);
    if(!layer)return;
    lv_area_t co;
    lv_obj_get_coords(obj,&co);
    /* A baked panel is one opaque image at 1:1: no scale, no recolor (the tint
     * is already in the pixels), so it takes the fast blit path. */
    const lv_image_dsc_t *bk=art_baked(obj,a,&co);
    if(bk){
        lv_draw_image_dsc_t dsc;
        lv_draw_image_dsc_init(&dsc);
        dsc.base.layer=layer;
        dsc.src=bk;
        dsc.opa=LV_OPA_COVER;
        dsc.recolor=lv_color_black();
        dsc.recolor_opa=LV_OPA_TRANSP;
        dsc.antialias=false;
        dsc.pivot.x=0;dsc.pivot.y=0;
        dsc.scale_x=256;dsc.scale_y=256;
        dsc.image_area=co;
        lv_draw_image(layer,&dsc,&co);
        return;
    }
    art_pieces(obj,layer,a,&co);
}

/* Nine-slice grid: three source columns/rows by three destination columns/rows. */
static void panel_blocks(art_t *a,rect_t r,int w,int h,int b)
{
    int sw[3]={b,r.w-2*b,b}, sh[3]={b,r.h-2*b,b};
    int dw[3]={b,w-2*b,b}, dh[3]={b,h-2*b,b};
    a->count=9;
    int sy=r.y,dy=0,k=0;
    for(unsigned j=0;j<3;j++){
        int sx=r.x,dx=0;
        for(unsigned i=0;i<3;i++){
            a->src[k].x=(int16_t)sx; a->src[k].y=(int16_t)sy;
            a->src[k].w=(int16_t)sw[i]; a->src[k].h=(int16_t)sh[j];
            a->dst[k].dx=(int16_t)dx; a->dst[k].dy=(int16_t)dy;
            a->dst[k].dw=(int16_t)dw[i]; a->dst[k].dh=(int16_t)dh[j];
            sx+=sw[i];dx+=dw[i];k++;
        }
        sy+=sh[j];dy+=dh[j];
    }
}

static void single_block(art_t *a,rect_t r,int w,int h)
{
    a->count=1;
    a->src[0].x=r.x;a->src[0].y=r.y;a->src[0].w=r.w;a->src[0].h=r.h;
    a->dst[0].dx=0;a->dst[0].dy=0;a->dst[0].dw=w;a->dst[0].dh=h;
}

static void art_deleted(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
    if(refs && !--refs){lw_asset_release(atlas);atlas=NULL;}
}

static lv_obj_t *art_create(lv_obj_t *p,int x,int y,int w,int h)
{
    if(w<1 || h<1)return NULL;
    const lv_image_dsc_t *img=atlas_image();
    if(!img)return NULL;
    art_t *a=lv_malloc_zeroed(sizeof(*a));
    if(!a){if(!refs){lw_asset_release(atlas);atlas=NULL;}return NULL;}
    a->atlas_w=img->header.w;a->atlas_h=img->header.h;
    lv_obj_t *o=lw_os_box(p,x,y,w,h,0,0,0);
    if(!o){lv_free(a);if(!refs){lw_asset_release(atlas);atlas=NULL;}return NULL;}
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(o,a);
    lv_obj_add_event_cb(o,art_draw,LV_EVENT_DRAW_MAIN,a);
    lv_obj_add_event_cb(o,art_deleted,LV_EVENT_DELETE,a);
    refs++;
    if(lw_ambience_phase()>=LW_NIGHT){
        lv_obj_set_style_image_recolor(o,lv_color_hex(0xb79496),0);
        lv_obj_set_style_image_recolor_opa(o,26,0);
    } else if(lw_ambience_phase()==LW_DUSK){
        lv_obj_set_style_image_recolor(o,lv_color_hex(0xd8a17d),0);
        lv_obj_set_style_image_recolor_opa(o,12,0);
    }
    return o;
}

/* State changes just rewrite the source rects and repaint one object. */
static void art_show(lv_obj_t *o,lw_skin_id_t id)
{
    art_t *a=lv_obj_get_user_data(o);
    if(!a || id>=SK_COUNT)return;
    if(a->count==9){
        panel_blocks(a,rects[id],lv_obj_get_width(o),lv_obj_get_height(o),a->border);
    } else {
        single_block(a,rects[id],lv_obj_get_width(o),lv_obj_get_height(o));
    }
    lv_obj_invalidate(o);
}

lv_obj_t *lw_skin_place(lv_obj_t *p,lw_skin_id_t id,int x,int y)
{
    if(id>=SK_COUNT)return NULL;
    rect_t r=rects[id];
    lv_obj_t *o=art_create(p,x,y,r.w,r.h);
    if(!o)return NULL;
    single_block(lv_obj_get_user_data(o),r,r.w,r.h);
    return o;
}

lv_obj_t *lw_skin_fit(lv_obj_t *p,lw_skin_id_t id,int x,int y,int w,int h)
{
    if(id>=SK_COUNT)return NULL;
    lv_obj_t *o=art_create(p,x,y,w,h);
    if(!o)return NULL;
    single_block(lv_obj_get_user_data(o),rects[id],w,h);
    return o;
}

lv_obj_t *lw_skin_panel(lv_obj_t *p,lw_skin_id_t id,int w,int h,int b)
{
    if(id>=SK_COUNT || b<1 || w<=2*b || h<=2*b)return NULL;
    rect_t r=rects[id];
    if(r.w<=2*b || r.h<=2*b)return NULL;
    lv_obj_t *o=art_create(p,0,0,w,h);
    if(!o)return NULL;
    art_t *a=lv_obj_get_user_data(o);
    a->border=b;
    panel_blocks(a,r,w,h,b);
    return o;
}

static void pressed(lv_event_t *e)
{
    lv_obj_t *button=lv_event_get_target_obj(e);
    art_t *a=lv_event_get_user_data(e);
    if(!a)return;
    lw_skin_id_t id=lv_event_get_code(e)==LV_EVENT_PRESSED ? a->pressed
                       : lv_obj_has_state(button,LV_STATE_CHECKED) ? a->selected : a->base;
    lv_obj_t *art=lv_obj_get_child(button,0);
    if(art)art_show(art,id);
}

bool lw_skin_button(lv_obj_t *o,lw_skin_id_t normal,lw_skin_id_t selected,lw_skin_id_t down)
{
    if(normal>=SK_COUNT || selected>=SK_COUNT || down>=SK_COUNT)return false;
    rect_t r=rects[normal];
    if(rects[selected].w!=r.w || rects[selected].h!=r.h || rects[down].w!=r.w || rects[down].h!=r.h){return false;}
    lv_obj_update_layout(o);
    int w=lv_obj_get_width(o),h=lv_obj_get_height(o);
    lv_obj_t *art=lw_skin_panel(o,normal,w,h,10);
    if(!art){return false;}
    art_t *a=lv_obj_get_user_data(art);
    a->base=normal;a->selected=selected;a->pressed=down;
    lv_obj_add_event_cb(o,pressed,LV_EVENT_PRESSED,a);
    lv_obj_add_event_cb(o,pressed,LV_EVENT_RELEASED,a);
    lv_obj_add_event_cb(o,pressed,LV_EVENT_PRESS_LOST,a);
    lv_obj_move_to_index(art,0);
    lv_obj_set_style_bg_opa(o,0,0);lv_obj_set_style_bg_opa(o,0,LV_STATE_PRESSED);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_shadow_width(o,0,0);
    for(unsigned i=0;i<lv_obj_get_child_count(o);i++){lv_obj_t *label=lv_obj_get_child(o,(int)i);if(lv_obj_check_type(label,&lv_label_class))lv_obj_set_style_text_color(label,lv_color_hex(0x75595c),0);}
    return true;
}

void lw_skin_select(lv_obj_t *o,bool selected)
{
    if(!o || lv_obj_has_state(o,LV_STATE_CHECKED)==selected){return;}
    if(selected){lv_obj_add_state(o,LV_STATE_CHECKED);}else{lv_obj_remove_state(o,LV_STATE_CHECKED);}
    lv_obj_send_event(o,LV_EVENT_RELEASED,NULL);
}

unsigned lw_skin_references(void){return refs;}

static void knob_follow(lv_event_t *e)
{
    lv_obj_t *slider=lv_event_get_target_obj(e),*knob=lv_event_get_user_data(e);int low=lv_slider_get_min_value(slider),high=lv_slider_get_max_value(slider),v=lv_slider_get_value(slider),h=lv_obj_get_height(slider);
    lv_obj_set_pos(knob,(lv_obj_get_width(slider)-32)/2,h-(v-low)*h/(high-low ? high-low : 1)-16);
}

void lw_skin_slider(lv_obj_t *slider)
{
    lv_obj_t *p=lv_obj_get_parent(slider);lv_obj_update_layout(slider);int x=lv_obj_get_x(slider),y=lv_obj_get_y(slider),h=lv_obj_get_height(slider);
    lv_obj_t *track=lw_skin_fit(p,SK_SLIDER,x+2,y,16,h);if(!track){return;}lv_obj_move_to_index(track,0);
    lv_obj_set_style_bg_opa(slider,0,LV_PART_MAIN);lv_obj_set_style_pad_hor(slider,6,LV_PART_MAIN);lv_obj_set_style_bg_opa(slider,170,LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider,0,LV_PART_KNOB);lv_obj_set_style_border_width(slider,0,LV_PART_KNOB);lv_obj_set_style_shadow_width(slider,0,LV_PART_KNOB);lv_obj_add_flag(slider,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_t *knob=lw_skin_fit(slider,SK_KNOB,-6,h/2-16,32,32);if(!knob){return;}
    lv_obj_add_event_cb(slider,knob_follow,LV_EVENT_VALUE_CHANGED,knob);
    lv_obj_add_event_cb(slider,knob_follow,LV_EVENT_REFRESH,knob);
    lv_obj_send_event(slider,LV_EVENT_REFRESH,NULL);
}
